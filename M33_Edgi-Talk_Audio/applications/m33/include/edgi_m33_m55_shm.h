/**
 * M33（分类/采音）与 M55（LVGL UI）之间的共享内存布局。
 *
 * 必须用 m33_m55_shared 区首地址，不可再用 m33_allocatable_shared（0x240FE000）：
 * cybsp 已在 .cy_sharedmem 里放了大量 SRF/IPC 缓冲，我们的结构体链接在其后，
 * 若 M55 仍读 0x240FE000 会得到 IPC 池内数据 → 出现 t=12、ts=0 等假值。
 *
 * cymem：CYMEM_*_m33_m55_shared_START = 0x261C0000（CM33/CM55 文档一致）。
 */
#ifndef EDGI_M33_M55_SHM_H
#define EDGI_M33_M55_SHM_H

#include <stdint.h>
#include <stddef.h>
#include "edgi_shm_cache.h"

#ifndef EDGI_M33_M55_SHM_SYSRAM_ADDR
#define EDGI_M33_M55_SHM_SYSRAM_ADDR  (0x261C0000u)
#endif

/* 与 protocol.h 中 AlarmResult 布局一致，8 字节 */
typedef struct
{
    uint8_t  alarm_type;
    uint8_t  confidence;
    uint16_t _reserved;
    uint32_t timestamp;
} edgi_shm_alarm_result_t;

typedef struct
{
    volatile uint32_t seq;
    edgi_shm_alarm_result_t alarm;
    /* 1=侦听/推理/上云开启；0=待机（省电）。由 KEY 长按切换，M55 只读。 */
    volatile uint8_t service_enable;
    uint8_t _pad_to_cmd[19];
    /* M55 shell 占用 uart2 时，经共享区触发 M33 命令（见 cm33） */
    volatile uint32_t m55_cmd_seq;
    volatile uint32_t m33_cmd_done;
    volatile uint8_t  m55_cmd_op;
    /* M55 写：0=无 1=WiFi驱动启 2=扫描/连接中 3=已联网；M33 读后可降负载/刷新绿灯 */
    volatile uint8_t  m55_wifi_state;
    volatile uint8_t  m55_ui_state;
    volatile uint8_t  m55_upload_state;
    volatile uint16_t m55_heartbeat;
    uint8_t _cmd_pad[18];
} edgi_m33_m55_shm_t;

#define EDGI_WIFI_SHM_IDLE    0u
#define EDGI_WIFI_SHM_DRV     1u
#define EDGI_WIFI_SHM_JOIN    2u  /* 扫描/连接中：M33 仅短暂降负载 */
#define EDGI_WIFI_SHM_READY   3u

/* Extra WiFi breadcrumbs for diagnostics; values above STABLE do not throttle M33. */
#define EDGI_WIFI_SHM_AUTO_ARMED  5u
#define EDGI_WIFI_SHM_AUTO_DELAY  6u
#define EDGI_WIFI_SHM_STARTING    7u
#define EDGI_WIFI_SHM_PC_WAIT     8u
#define EDGI_WIFI_SHM_RECONNECT   9u
#define EDGI_WIFI_SHM_THREAD_FAIL 0xE1u
#define EDGI_WIFI_SHM_START_FAIL  0xE2u
#define EDGI_WIFI_SHM_JOIN_FAIL   0xE3u

/* M55 writes these lightweight breadcrumbs; M33/OpenOCD can read them even when UART RX is dead. */
#define EDGI_M55_UI_NONE            0u
#define EDGI_M55_UI_MAIN            1u
#define EDGI_M55_UI_LVGL_THREAD     2u
#define EDGI_M55_UI_ALARM_INIT      3u
#define EDGI_M55_UI_ALARM_READY     4u
#define EDGI_M55_UI_LVGL_FAIL       0xE1u
#define EDGI_M55_UI_NOT_INIT        0xE2u
#define EDGI_M55_UI_NO_SCREEN       0xE3u
#define EDGI_M55_UI_BAD_SIZE        0xE4u

#define EDGI_M55_UPLOAD_NONE        0u
#define EDGI_M55_UPLOAD_THREAD      1u
#define EDGI_M55_UPLOAD_AUTO_ON     2u
#define EDGI_M55_UPLOAD_POSTING     3u
#define EDGI_M55_UPLOAD_OK          4u
#define EDGI_M55_UPLOAD_FAIL        5u
#define EDGI_WIFI_SHM_STABLE  4u  /* 联网稳定：M33 恢复正常绿灯/AI/KEY */

/* m55_cmd_op */
#define EDGI_SHM_CMD_NONE      0u
#define EDGI_SHM_CMD_M33_STAT  1u
#define EDGI_SHM_CMD_KEY_DBG   2u
#define EDGI_SHM_CMD_KEY_REC   3u
#define EDGI_SHM_CMD_TEST_FIRE 4u
#define EDGI_SHM_CMD_DEMO_CAL_1 5u
#define EDGI_SHM_CMD_DEMO_CAL_2 6u
#define EDGI_SHM_CMD_DEMO_CAL_3 7u
#define EDGI_SHM_CMD_DEMO_CAL_4 8u

/* M33 只 flush 此前字段，避免 publish 把 M55 写入的 cm33 命令冲掉 */
#define EDGI_M33_M55_SHM_M33_DATA_BYTES  offsetof(edgi_m33_m55_shm_t, m55_cmd_seq)
#define EDGI_M33_M55_SHM_CMD_BYTES       (sizeof(edgi_m33_m55_shm_t) - EDGI_M33_M55_SHM_M33_DATA_BYTES)

static inline void edgi_shm_invalidate_m33_cmd(void *shm_base)
{
    edgi_shm_cache_invalidate((uint8_t *)shm_base + EDGI_M33_M55_SHM_M33_DATA_BYTES,
                              (int)EDGI_M33_M55_SHM_CMD_BYTES);
}

void edgi_m33_shm_poll_remote_cmds(void);

uint8_t edgi_m33_shm_wifi_state_read(void);

#if defined(__GNUC__)
#define EDGI_M33_M55_COARSE_BARRIER()  __asm volatile("" ::: "memory")
#if defined(__ARM_ARCH) && (__ARM_ARCH >= 7)
#define EDGI_M33_M55_DMB_ISH()         __asm volatile("dmb ish" ::: "memory")
#else
#define EDGI_M33_M55_DMB_ISH()         EDGI_M33_M55_COARSE_BARRIER()
#endif
#else
#define EDGI_M33_M55_COARSE_BARRIER()  do {} while (0)
#define EDGI_M33_M55_DMB_ISH()         do {} while (0)
#endif

static inline edgi_m33_m55_shm_t *edgi_m33_m55_shm_get(void)
{
    return (edgi_m33_m55_shm_t *)(uintptr_t)EDGI_M33_M55_SHM_SYSRAM_ADDR;
}

/* M33: clear only its own cache line and publish the first frame. */
void edgi_m33_m55_shm_init(void);

/* M33：由 edgi_m33_m55_shm.c 实现 */
void edgi_m33_m55_shm_publish(const edgi_shm_alarm_result_t *alarm);

void edgi_m33_service_set_enable(uint8_t enable);
uint8_t edgi_m33_service_get_enable(void);

#endif /* EDGI_M33_M55_SHM_H */
