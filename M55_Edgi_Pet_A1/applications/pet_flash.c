#include "pet_flash.h"
#include <rtthread.h>
#include <board.h>
#include <string.h>
#include "cycfg_qspi_memslot.h"
#include "cy_smif.h"
#include "../../pet_shared/pet_flash_gate.h"

static cy_stc_smif_context_t s_context;
static cy_stc_smif_mem_config_t *s_config;
static uint32_t s_token;

int pet_flash_gate_ready(void) { return PET_FLASH_GATE->ready == PET_FLASH_GATE_MAGIC; }
unsigned long pet_flash_parks(void) { return PET_FLASH_GATE->parks; }

static int acquire(void)
{
    uint32_t started = rt_tick_get_millisecond();
    if (!pet_flash_gate_ready() || PET_FLASH_GATE->request) return 0;
    s_token++;
    if (!s_token || s_token == PET_FLASH_GATE->parked) s_token = PET_FLASH_GATE->parked + 1UL;
    if (!s_token) s_token = 1;
    PET_FLASH_GATE->request = s_token;
    __DSB();
    __SEV();
    while (PET_FLASH_GATE->parked != s_token) {
        if ((uint32_t)(rt_tick_get_millisecond() - started) >= 200UL) {
            PET_FLASH_GATE->request = 0; __DSB(); return 0;
        }
        /* Re-send across the peer's check-to-WFE window; no interrupt/XIP
         * handler is needed while that peer is already parked in SRAM. */
        __SEV();
        rt_thread_mdelay(1);
    }
    __DSB();
    return 1;
}

static void release(void)
{
    PET_FLASH_GATE->request = 0;
    __DSB();
    __SEV();
}

/* This routine and every called SMIF/SysLib function run from ITCM.
 * The peer has already parked in SRAM; payload and config are in RAM.
 * Never return to XIP while the chip still reports busy. */
__attribute__((section(".cy_ramfunc"), noinline))
static int transfer(unsigned operation, uint32_t address, uint8_t *bytes, uint32_t size)
{
    uint32_t primask = __get_PRIMASK();
    cy_en_smif_status_t status, ready;
    uint8_t erase_address[3];
    __disable_irq();
    __DSB();
    if (operation == 0) {
        status = Cy_SMIF_MemRead(SMIF0_CORE, s_config, address, bytes, size, &s_context);
    } else if (operation == 1) {
        status = Cy_SMIF_MemWrite(SMIF0_CORE, s_config, address, bytes, size, &s_context);
    } else {
        erase_address[0] = (uint8_t)(address >> 16);
        erase_address[1] = (uint8_t)(address >> 8);
        erase_address[2] = (uint8_t)address;
        status = Cy_SMIF_MemCmdWriteEnable(SMIF0_CORE, s_config, &s_context);
        if (status == CY_SMIF_SUCCESS)
            status = Cy_SMIF_MemCmdSectorErase(SMIF0_CORE, s_config, erase_address, &s_context);
        if (status == CY_SMIF_SUCCESS)
            status = Cy_SMIF_MemIsReady(SMIF0_CORE, s_config, 1000000UL, &s_context);
    }
    ready = Cy_SMIF_MemIsReady(SMIF0_CORE, s_config, 2000000UL, &s_context);
    if (ready != CY_SMIF_SUCCESS) {
        /* Fault recovery cannot fetch/log from unavailable flash. Reset also
         * clears the peer's park; the journal keeps the previous valid record. */
        /* Equivalent to CMSIS SystemReset, kept inline here: the debug build's
         * non-forced-inline CMSIS helper otherwise lands in external flash. */
        __DSB();
        SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) |
                     (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) | SCB_AIRCR_SYSRESETREQ_Msk;
        __DSB();
        while (1) { __NOP(); }
    }
    __DSB();
    __set_PRIMASK(primask);
    return status == CY_SMIF_SUCCESS;
}

static int bounds(u32 offset, u32 size)
{
    return size && offset < PET_FLASH_DATA_SIZE && size <= PET_FLASH_DATA_SIZE - offset;
}

static int read_record(void *context, u32 offset, u8 *out, u32 size)
{
    int result;
    (void)context;
    if (!out || !bounds(offset, size) || !acquire()) return 0;
    result = transfer(0, PET_FLASH_DATA_ADDRESS - 0x60000000UL + offset, out, size);
    release();
    return result;
}

static int program_record(void *context, u32 offset, const u8 *bytes, u32 size)
{
    int result;
    (void)context;
    if (!bytes || size != PET_JOURNAL_RECORD || offset % PET_JOURNAL_PAGE ||
        !bounds(offset, size) || !acquire()) return 0;
    result = transfer(1, PET_FLASH_DATA_ADDRESS - 0x60000000UL + offset, (u8 *)bytes, size);
    release();
    return result;
}

static int erase_sector(void *context, u32 offset, u32 size)
{
    int result;
    (void)context;
    if (size != PET_FLASH_SECTOR_SIZE || offset % PET_FLASH_SECTOR_SIZE ||
        !bounds(offset, size) || !acquire()) return 0;
    result = transfer(2, PET_FLASH_DATA_ADDRESS - 0x60000000UL + offset, RT_NULL, size);
    release();
    return result;
}

int pet_flash_init(PetJournalIo *io)
{
    cy_stc_smif_hybrid_region_info_t *region = RT_NULL;
    uint32_t started = rt_tick_get_millisecond();
    if (!io) return 0;
    s_config = smif0MemConfigs[0];
    if (!s_config || !s_config->deviceCfg || s_config->baseAddress != 0x60000000UL ||
        s_config->deviceCfg->memSize < 0xF20000UL ||
        s_config->deviceCfg->programSize != PET_JOURNAL_PAGE ||
        s_config->deviceCfg->numOfAddrBytes != 3 ||
        Cy_SMIF_MemLocateHybridRegion(s_config, &region, 0xF00000UL) != CY_SMIF_SUCCESS ||
        !region || region->eraseSize != PET_FLASH_SECTOR_SIZE ||
        region->regionAddress + region->sectorsCount * region->eraseSize < 0xF20000UL)
        return 0;
    memset(&s_context, 0, sizeof(s_context));
    s_context.timeout = 10000;
    s_context.memReadyPollDelay = 1;
    while (!pet_flash_gate_ready()) {
        if ((uint32_t)(rt_tick_get_millisecond() - started) >= 1000UL) return 0;
        rt_thread_mdelay(5);
    }
    io->context = RT_NULL;
    io->read = read_record; io->program = program_record; io->erase = erase_sector;
    rt_kprintf("[A4] pet flash: base=0x60f00000 sectors=2 erase=65536 page=256 gate=ready\n");
    return 1;
}
