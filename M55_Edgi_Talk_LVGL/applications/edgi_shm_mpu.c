#include "edgi_shm_mpu.h"
#include "edgi_m33_m55_shm.h"
#include <rtthread.h>

#if EDGI_SHM_MPU_ENABLE
#include "armv8m_mpu.h"

#ifndef EDGI_SHM_MPU_REGION
#define EDGI_SHM_MPU_REGION  7u
#endif
#ifndef EDGI_SHM_MPU_ATTR_IDX
#define EDGI_SHM_MPU_ATTR_IDX  7u
#endif
#endif

void edgi_shm_mpu_init(void)
{
#if !EDGI_SHM_MPU_ENABLE
    return;
#else
    if (ARM_MPU_TYPE() == 0u)
        return;

    ARM_MPU_SetMemAttr(EDGI_SHM_MPU_ATTR_IDX,
                       ARM_MPU_ATTR(ARM_MPU_ATTR_NON_CACHEABLE, ARM_MPU_ATTR_NON_CACHEABLE));

    ARM_MPU_SetRegion(EDGI_SHM_MPU_REGION,
                      ARM_MPU_RBAR(EDGI_M33_M55_SHM_SYSRAM_ADDR, ARM_MPU_SH_INNER, 0, 1, 1),
                      ARM_MPU_RLAR(EDGI_M33_M55_SHM_SYSRAM_ADDR + 63u, EDGI_SHM_MPU_ATTR_IDX));

    if ((MPU->CTRL & MPU_CTRL_ENABLE_Msk) == 0u)
        ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_HFNMIENA_Msk);

    rt_kprintf("[shm] MPU non-cache @0x%08X (64B)\n",
               (unsigned)EDGI_M33_M55_SHM_SYSRAM_ADDR);
#endif
}
