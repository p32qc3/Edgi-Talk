/**
 * @file pet_flash_gate.h
 * @brief Flash访问协调接口 - M33与M55共享头文件
 *
 * 用途：
 * - M55保存宠物数据时，需要擦写外部Flash (0x60F00000..0x60F1FFFF)
 * - M33的代码段也在外部Flash，擦写期间不能取指
 * - 通过此协调机制，M55请求写入，M33进入RAM等待
 *
 * 协议：
 * - M33启动时调用pet_flash_gate_init()，在轮询或空闲钩子中调用pet_flash_gate_poll()
 * - M55写入前递增request，等待parked == request，完成后再次递增request
 * - M33看到request变化，进入RAM循环等待，直到request再次变化
 */

#ifndef PET_FLASH_GATE_H
#define PET_FLASH_GATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PET_FLASH_GATE_MAGIC 0x50455447UL  /* "PETG" */
#define PET_FLASH_GATE_ADDR  0x240FFF80UL  /* M55 allocatable shared SRAM tail */

/**
 * 共享控制结构
 * 位于固定地址，M33和M55都能访问
 */
typedef struct {
    volatile uint32_t ready;      /* M33写入MAGIC表示已初始化 */
    volatile uint32_t request;    /* M55递增请求写入，M33看到变化后park */
    volatile uint32_t parked;     /* M33写入当前看到的request值，表示已park */
    volatile uint32_t parks;      /* M33累计park次数（统计用） */
    volatile uint32_t heartbeat;  /* M33累计轮询次数（检活用） */
} PetFlashGate;

#define PET_FLASH_GATE  ((PetFlashGate *)PET_FLASH_GATE_ADDR)

/**
 * M33端接口：初始化协调机制
 * 在M33启动早期调用，清零所有字段并设置ready标志
 */
void pet_flash_gate_init(void);

/* Starts the dedicated high-priority M33 polling thread. */
int pet_flash_gate_start(void);

/**
 * M33端接口：轮询检查M55的写入请求
 * 必须在RAM中执行，不能访问外部Flash、调用库函数或中断
 * 通常在空闲任务或定时器中周期调用（建议10ms）
 */
void pet_flash_gate_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* PET_FLASH_GATE_H */
