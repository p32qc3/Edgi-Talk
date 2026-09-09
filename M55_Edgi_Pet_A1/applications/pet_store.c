#include "pet_store.h"
#include "pet_flash.h"
#include <rtthread.h>
#include <string.h>
#include "../../pet_shared/pet_flash_gate.h"

static PetJournal s_journal;
static struct rt_thread s_worker;
rt_align(RT_ALIGN_SIZE) static u8 s_stack[4096];
static struct rt_semaphore s_wakeup;
static PetStoreStatus s_status;
static PetSaved s_request;
static PetSaved s_committed;
static u32 s_next_id;

int pet_store_growth_pending(const PetSaved *saved)
{
    PetSaved a, b;
    u8 encoded_a[SAVE_SIZE], encoded_b[SAVE_SIZE];
    rt_base_t level;
    if (!saved) return 1;
    a = *saved;
    level = rt_hw_interrupt_disable(); b = s_committed; rt_hw_interrupt_enable(level);
    a.window_ms = b.window_ms = 0;
    return !save_encode(&a, 0, encoded_a) || !save_encode(&b, 0, encoded_b) ||
        memcmp(encoded_a, encoded_b, SAVE_SIZE) != 0;
}

void pet_store_get(PetStoreStatus *out)
{
    rt_base_t level;
    if (!out) return;
    level = rt_hw_interrupt_disable(); *out = s_status; rt_hw_interrupt_enable(level);
}
int pet_store_busy(void)
{
    PetStoreStatus state;
    pet_store_get(&state);
    return state.pending || state.saving;
}
int pet_store_request(const PetSaved *saved)
{
    rt_base_t level;
    if (!saved) return -RT_EINVAL;
    level = rt_hw_interrupt_disable();
    if (!s_status.ready || s_status.pending || s_status.saving) {
        rt_hw_interrupt_enable(level); return -RT_EBUSY;
    }
    s_request = *saved;
    s_status.request_id = ++s_next_id;
    s_status.pending = 1;
    rt_hw_interrupt_enable(level);
    rt_sem_release(&s_wakeup);
    return RT_EOK;
}
static void store_loop(void *parameter)
{
    PetSaved saved;
    u32 id;
    int result;
    rt_base_t level;
    (void)parameter;
    while (1) {
        rt_sem_take(&s_wakeup, RT_WAITING_FOREVER);
        level = rt_hw_interrupt_disable();
        saved = s_request; id = s_status.request_id;
        s_status.pending = 0; s_status.saving = 1;
        rt_hw_interrupt_enable(level);
        result = journal_commit(&s_journal, &saved);
        level = rt_hw_interrupt_disable();
        s_status.saving = 0; s_status.error = result < 0 ? result : 0;
        s_status.completed_id = id;
        if (result == JOURNAL_LOADED) {
            s_committed = saved;
            s_status.generation = s_journal.generation;
            s_status.last_saved_at = rt_tick_get_millisecond();
            s_status.saved_trust = saved.trust_x100; s_status.writes++;
        } else s_status.failures++;
        rt_hw_interrupt_enable(level);
        rt_kprintf("[A4] save id=%lu result=%d gen=%lu trust_x100=%u interactions=%lu window=%lu\n",
                   (unsigned long)id, result, (unsigned long)s_journal.generation,
                   saved.trust_x100, (unsigned long)saved.interactions, (unsigned long)saved.window_ms);
    }
}
int pet_store_init(PetSaved *restored)
{
    PetJournalIo io;
    int opened, result;
    if (!restored) return -1;
    memset(&s_status, 0, sizeof(s_status));
    if (!pet_flash_init(&io)) { s_status.error = -10; return -1; }
    opened = journal_open(&s_journal, &io, PET_FLASH_SECTOR_SIZE);
    s_status.damaged = s_journal.damaged_records;
    s_status.generation = s_journal.generation;
    if (opened < 0) { s_status.error = opened; return -1; }
    if (opened == JOURNAL_LOADED) {
        *restored = s_journal.saved;
        s_committed = *restored;
        s_status.loaded = 1; s_status.saved_trust = restored->trust_x100;
    }
    result = rt_sem_init(&s_wakeup, "pet_save", 0, RT_IPC_FLAG_FIFO);
    if (result == RT_EOK)
        result = rt_thread_init(&s_worker, "pet_save", store_loop, RT_NULL,
                                s_stack, sizeof(s_stack), 23, 5);
    if (result != RT_EOK) { s_status.error = result; return opened == JOURNAL_LOADED ? 1 : -1; }
    s_status.ready = 1;
    result = rt_thread_startup(&s_worker);
    if (result != RT_EOK) {
        s_status.ready = 0; s_status.error = result;
        return opened == JOURNAL_LOADED ? 1 : -1;
    }
    rt_kprintf("[A4] store open=%d gen=%lu damaged=%lu trust_x100=%u\n", opened,
               (unsigned long)s_status.generation, (unsigned long)s_status.damaged, s_status.saved_trust);
    return opened;
}
