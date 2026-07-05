/**
 * M55 侧只读：与 M33 上 m33/include/edgi_m33_m55_shm.h 布局与地址须完全一致。
 * 共享区在 m33_m55_shared（0x261C0000），勿使用 0x240FE000（与 cybsp IPC 冲突）。
 */
#ifndef EDGI_M33_M55_SHM_H
#define EDGI_M33_M55_SHM_H

#include <stdint.h>
#include <stddef.h>
#include "edgi_shm_cache.h"

#ifndef EDGI_M33_M55_SHM_SYSRAM_ADDR
#define EDGI_M33_M55_SHM_SYSRAM_ADDR  (0x261C0000u)
#endif

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
    volatile uint8_t service_enable;
    uint8_t _pad_to_cmd[19];
    volatile uint32_t m55_cmd_seq;
    volatile uint32_t m33_cmd_done;
    volatile uint8_t  m55_cmd_op;
    volatile uint8_t  m55_wifi_state;
    volatile uint8_t  m55_ui_state;
    volatile uint8_t  m55_upload_state;
    volatile uint16_t m55_heartbeat;
    uint8_t _cmd_pad[18];
} edgi_m33_m55_shm_t;

#define EDGI_WIFI_SHM_IDLE    0u
#define EDGI_WIFI_SHM_DRV     1u
#define EDGI_WIFI_SHM_JOIN    2u
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

/* M55 progress breadcrumbs for OpenOCD/M33 diagnostics. */
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
#define EDGI_WIFI_SHM_STABLE  4u

#define EDGI_WIFI_PC_BOX_ADDR   (EDGI_M33_M55_SHM_SYSRAM_ADDR + 0x100u)
#define EDGI_WIFI_PC_BOX_MAGIC  0x49464957u

#define EDGI_WIFI_PC_IDLE       0u
#define EDGI_WIFI_PC_PENDING    1u
#define EDGI_WIFI_PC_JOINING    2u
#define EDGI_WIFI_PC_CONNECTED  3u
#define EDGI_WIFI_PC_FAILED     4u
#define EDGI_WIFI_PC_INVALID    5u

typedef struct
{
    volatile uint32_t magic;
    volatile uint32_t request_seq;
    volatile uint32_t done_seq;
    volatile uint32_t status;
    volatile int32_t result;
    volatile uint8_t ssid_len;
    volatile uint8_t key_len;
    uint8_t _reserved0[2];
    char ssid[33];
    char key[65];
    uint8_t _reserved1[2];
    volatile uint32_t checksum;
} edgi_wifi_pc_box_t;

static inline edgi_wifi_pc_box_t *edgi_wifi_pc_box_get(void)
{
    return (edgi_wifi_pc_box_t *)(uintptr_t)EDGI_WIFI_PC_BOX_ADDR;
}

void edgi_m55_shm_set_wifi_state(uint8_t state);
void edgi_m55_shm_set_ui_state(uint8_t state);
void edgi_m55_shm_set_upload_state(uint8_t state);
void edgi_m55_shm_heartbeat(void);

#define EDGI_SHM_CMD_NONE      0u
#define EDGI_SHM_CMD_M33_STAT  1u
#define EDGI_SHM_CMD_KEY_DBG   2u
#define EDGI_SHM_CMD_KEY_REC   3u
#define EDGI_SHM_CMD_TEST_FIRE 4u
#define EDGI_SHM_CMD_DEMO_CAL_1 5u
#define EDGI_SHM_CMD_DEMO_CAL_2 6u
#define EDGI_SHM_CMD_DEMO_CAL_3 7u
#define EDGI_SHM_CMD_DEMO_CAL_4 8u

#define EDGI_M33_M55_SHM_M33_DATA_BYTES  offsetof(edgi_m33_m55_shm_t, m55_cmd_seq)
#define EDGI_M33_M55_SHM_CMD_BYTES       (sizeof(edgi_m33_m55_shm_t) - EDGI_M33_M55_SHM_M33_DATA_BYTES)

/* M55 只 invalidate 此范围，勿 invalidate 全结构体（会破坏 M33 正在写的 cache） */
static inline void edgi_shm_invalidate_m33_data(void *shm_base)
{
    edgi_shm_cache_invalidate(shm_base, (int)EDGI_M33_M55_SHM_M33_DATA_BYTES);
}

static inline void edgi_shm_invalidate_m33_cmd(void *shm_base)
{
    edgi_shm_cache_invalidate((uint8_t *)shm_base + EDGI_M33_M55_SHM_M33_DATA_BYTES,
                              (int)EDGI_M33_M55_SHM_CMD_BYTES);
}

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

/* M55 只读：0=待机 1=侦听开启 */
static inline uint8_t edgi_shm_service_enabled(void)
{
    volatile edgi_m33_m55_shm_t *shm = edgi_m33_m55_shm_get();
    return shm->service_enable ? 1u : 0u;
}

#endif /* EDGI_M33_M55_SHM_H */
