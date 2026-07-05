/**
 * 双核共享 RAM 的 D-Cache 维护（M55 必读 invalidate；M33 写后 flush）。
 * RT-Thread 未开 RT_USING_CACHE 时 rt_hw_cpu_dcache_ops 为空，须用 CMSIS SCB。
 */
#ifndef EDGI_SHM_CACHE_H
#define EDGI_SHM_CACHE_H

#include <stdint.h>
#include <stddef.h>
#include "cy_device_headers.h"

#ifndef EDGI_SHM_CACHE_LINE_SIZE
#define EDGI_SHM_CACHE_LINE_SIZE 32
#endif

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
static inline void edgi_shm_cache_invalidate(void *addr, int size)
{
    if (addr == NULL || size <= 0)
        return;
    {
        uintptr_t a = (uintptr_t)addr;
        uintptr_t start = a & ~(uintptr_t)(EDGI_SHM_CACHE_LINE_SIZE - 1U);
        uintptr_t end = a + (uintptr_t)(unsigned)size;
        int32_t total = (int32_t)(end - start);

        SCB_InvalidateDCache_by_Addr((void *)start, total);
        __DSB();
        __ISB();
    }
}

static inline void edgi_shm_cache_flush(void *addr, int size)
{
    if (addr == NULL || size <= 0)
        return;
    {
        uintptr_t a = (uintptr_t)addr;
        uintptr_t start = a & ~(uintptr_t)(EDGI_SHM_CACHE_LINE_SIZE - 1U);
        uintptr_t end = a + (uintptr_t)(unsigned)size;
        int32_t total = (int32_t)(end - start);

        SCB_CleanDCache_by_Addr((void *)start, total);
        __DSB();
        __ISB();
    }
}

#else /* no dcache */

static inline void edgi_shm_cache_invalidate(void *addr, int size)
{
    (void)addr;
    (void)size;
}

static inline void edgi_shm_cache_flush(void *addr, int size)
{
    (void)addr;
    (void)size;
}

#endif /* __DCACHE_PRESENT */

#endif /* EDGI_SHM_CACHE_H */
