#include <rtthread.h>
#include <board.h>
#include "../../pet_shared/pet_flash_gate.h"

#define PET_FLASH_GATE_STACK_SIZE 1024
#define PET_FLASH_GATE_PRIORITY 2

static rt_thread_t s_gate_thread;

/* No interrupt, library, RTOS or XIP access between acknowledging a request
 * and its release. The caller is the BSP sleep loop before RTOS startup. */
__attribute__((section(".cy_ramfunc"), noinline))
void pet_flash_gate_poll(void)
{
    PetFlashGate *gate = PET_FLASH_GATE;
    uint32_t request = gate->request, primask;
    if (!request) { gate->heartbeat++; return; }
    primask = __get_PRIMASK();
    __disable_irq();
    if (gate->request == request) {
        __DSB();
        gate->parks++;
        gate->parked = request;
        __DSB();
        while (gate->request == request) { __DMB(); }
        gate->parked = 0;
        __DSB();
    }
    __set_PRIMASK(primask);
}

void pet_flash_gate_init(void)
{
    PET_FLASH_GATE->ready = 0;
    PET_FLASH_GATE->request = 0;
    PET_FLASH_GATE->parked = 0;
    PET_FLASH_GATE->parks = 0;
    PET_FLASH_GATE->heartbeat = 0;
    __DSB();
    PET_FLASH_GATE->ready = PET_FLASH_GATE_MAGIC;
}

static void pet_flash_gate_entry(void *parameter)
{
    (void)parameter;
    while (1) {
        pet_flash_gate_poll();
        rt_thread_mdelay(10);
    }
}

int pet_flash_gate_start(void)
{
    if (s_gate_thread) return RT_EOK;
    pet_flash_gate_init();
    s_gate_thread = rt_thread_create("flash_gate", pet_flash_gate_entry,
        RT_NULL, PET_FLASH_GATE_STACK_SIZE, PET_FLASH_GATE_PRIORITY, 10);
    if (!s_gate_thread) return -RT_ENOMEM;
    rt_thread_startup(s_gate_thread);
    return RT_EOK;
}
