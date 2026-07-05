#ifndef EDGI_SHM_MPU_H
#define EDGI_SHM_MPU_H

/* 默认关闭：CM55 上 region 7 常被 Cypress BSP 占用，强行改 MPU 会导致屏黑/shell 无响应 */
#ifndef EDGI_SHM_MPU_ENABLE
#define EDGI_SHM_MPU_ENABLE  0
#endif

void edgi_shm_mpu_init(void);

#endif /* EDGI_SHM_MPU_H */
