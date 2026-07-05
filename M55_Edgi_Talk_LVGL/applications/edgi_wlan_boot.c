/*
 * WiFi 配置写入 Flash；按 RESET(XRES) 清除并要求重新配网；
 * KEY 关机后的软件复位仍从 Flash 自动连回。
 */
#include <rtthread.h>
#include <fal.h>
#include <wlan_cfg.h>
#include <wlan_mgnt.h>
#include "cy_syslib.h"
#include "edgi_m33_m55_shm.h"
#include "edgi_shm_cache.h"
#include <string.h>
#include <stdint.h>

extern void edgi_alarm_set_auto_upload(int enable);
extern void edgi_m55_shm_set_wifi_state(uint8_t state);
extern int edgi_wifi_start_auto(void);

#define EDGI_WLAN_CFG_PART   "wlan_cfg"
#define EDGI_WLAN_CFG_MAXLEN 4096

#ifndef EDGI_WIFI_AUTO_START
#define EDGI_WIFI_AUTO_START 0
#endif

#ifndef EDGI_WIFI_AUTO_JOIN
#define EDGI_WIFI_AUTO_JOIN 1
#endif

#ifndef EDGI_WIFI_AUTO_JOIN_DELAY_MS
#define EDGI_WIFI_AUTO_JOIN_DELAY_MS 5000
#endif

#ifndef EDGI_WIFI_DRIVER_SETTLE_MS
#define EDGI_WIFI_DRIVER_SETTLE_MS 3000
#endif

#ifndef EDGI_WIFI_AUTO_JOIN_RETRY_MS
#define EDGI_WIFI_AUTO_JOIN_RETRY_MS 15000
#endif

#ifndef EDGI_WIFI_AUTO_JOIN_MAX_RETRY
#define EDGI_WIFI_AUTO_JOIN_MAX_RETRY 0
#endif

#ifndef EDGI_WIFI_DEFAULT_SSID
#define EDGI_WIFI_DEFAULT_SSID "edgi"
#endif

#ifndef EDGI_WIFI_DEFAULT_KEY
#define EDGI_WIFI_DEFAULT_KEY ""
#endif

#ifndef EDGI_WIFI_CLEAR_CFG_ON_XRES
#define EDGI_WIFI_CLEAR_CFG_ON_XRES 0
#endif

#ifndef EDGI_WIFI_FLASH_PERSIST
#define EDGI_WIFI_FLASH_PERSIST 0
#endif

static const struct fal_partition *s_wlan_part;
static rt_bool_t s_wlan_alarm_events_registered;
static volatile rt_bool_t s_wlan_stable_arm;
static volatile rt_uint32_t s_wlan_state_gen;
static volatile rt_uint32_t s_pc_active_seq;

_Static_assert(sizeof(edgi_wifi_pc_box_t) == 128u,
               "PC WiFi provisioning mailbox layout changed");

static uint32_t edgi_pc_box_checksum(uint32_t seq,
                                     const char *ssid, uint8_t ssid_len,
                                     const char *key, uint8_t key_len)
{
    uint32_t hash = 2166136261u;
    unsigned i;
    uint8_t seq_bytes[4];

    seq_bytes[0] = (uint8_t)(seq & 0xffu);
    seq_bytes[1] = (uint8_t)((seq >> 8) & 0xffu);
    seq_bytes[2] = (uint8_t)((seq >> 16) & 0xffu);
    seq_bytes[3] = (uint8_t)((seq >> 24) & 0xffu);
    for (i = 0; i < 4u; i++)
    {
        hash ^= seq_bytes[i];
        hash *= 16777619u;
    }
    hash ^= ssid_len;
    hash *= 16777619u;
    hash ^= key_len;
    hash *= 16777619u;
    for (i = 0; i < ssid_len; i++)
    {
        hash ^= (uint8_t)ssid[i];
        hash *= 16777619u;
    }
    for (i = 0; i < key_len; i++)
    {
        hash ^= (uint8_t)key[i];
        hash *= 16777619u;
    }
    return hash;
}

static void edgi_pc_box_flush(edgi_wifi_pc_box_t *box)
{
    EDGI_M33_M55_DMB_ISH();
    edgi_shm_cache_flush(box, (int)sizeof(*box));
}

static void edgi_pc_box_init(void)
{
    edgi_wifi_pc_box_t *box = edgi_wifi_pc_box_get();

    edgi_shm_cache_invalidate(box, (int)sizeof(*box));
    if (box->magic == EDGI_WIFI_PC_BOX_MAGIC)
        return;

    rt_memset(box, 0, sizeof(*box));
    box->magic = EDGI_WIFI_PC_BOX_MAGIC;
    edgi_pc_box_flush(box);
}

static void edgi_pc_box_finish(uint32_t status, int result)
{
    edgi_wifi_pc_box_t *box = edgi_wifi_pc_box_get();
    uint32_t active = s_pc_active_seq;

    if (active == 0u)
        return;

    edgi_shm_cache_invalidate(box, (int)sizeof(*box));
    if (box->request_seq == active)
    {
        box->result = result;
        box->status = status;
        box->done_seq = active;
        edgi_pc_box_flush(box);
    }
    s_pc_active_seq = 0u;
}

static void edgi_pc_box_poll(void)
{
    edgi_wifi_pc_box_t *box = edgi_wifi_pc_box_get();
    char ssid[33];
    char key[65];
    uint32_t seq;
    uint32_t checksum;
    uint8_t ssid_len;
    uint8_t key_len;
    rt_err_t err;

    edgi_shm_cache_invalidate(box, (int)sizeof(*box));
    if (box->magic != EDGI_WIFI_PC_BOX_MAGIC ||
        box->status != EDGI_WIFI_PC_PENDING ||
        box->request_seq == 0u ||
        box->request_seq == box->done_seq ||
        s_pc_active_seq != 0u)
        return;

    seq = box->request_seq;
    ssid_len = box->ssid_len;
    key_len = box->key_len;
    if (ssid_len == 0u || ssid_len > 32u || key_len > 64u)
    {
        box->result = -RT_EINVAL;
        box->status = EDGI_WIFI_PC_INVALID;
        box->done_seq = seq;
        edgi_pc_box_flush(box);
        return;
    }

    rt_memcpy(ssid, box->ssid, ssid_len);
    ssid[ssid_len] = '\0';
    rt_memcpy(key, box->key, key_len);
    key[key_len] = '\0';
    checksum = edgi_pc_box_checksum(seq, ssid, ssid_len, key, key_len);
    if (checksum != box->checksum)
    {
        box->result = -RT_ERROR;
        box->status = EDGI_WIFI_PC_INVALID;
        box->done_seq = seq;
        edgi_pc_box_flush(box);
        return;
    }

    s_pc_active_seq = seq;
    rt_memset(box->key, 0, sizeof(box->key));
    box->key_len = 0u;
    box->result = 0;
    box->status = EDGI_WIFI_PC_JOINING;
    edgi_pc_box_flush(box);
    edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_JOIN);
    rt_kprintf("[wifi] PC provisioning: joining ssid=%s\n", ssid);
    err = rt_wlan_connect(ssid, key_len ? key : RT_NULL);
    rt_memset(key, 0, sizeof(key));
    if (err != RT_EOK)
    {
        edgi_pc_box_finish(EDGI_WIFI_PC_FAILED, (int)err);
        edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_PC_WAIT);
        rt_kprintf("[wifi] PC provisioning request failed (%d)\n", (int)err);
    }
}

#ifndef EDGI_ALARM_UPLOAD_DELAY_AFTER_READY_MS
#define EDGI_ALARM_UPLOAD_DELAY_AFTER_READY_MS 8000
#endif

#ifndef EDGI_ALARM_UPLOAD_AUTO_ON_WIFI_READY
#define EDGI_ALARM_UPLOAD_AUTO_ON_WIFI_READY 1
#endif

static void edgi_alarm_upload_arm_delayed(void *param)
{
    rt_uint32_t gen = (rt_uint32_t)(uintptr_t)param;

    rt_thread_mdelay(EDGI_ALARM_UPLOAD_DELAY_AFTER_READY_MS);
    if (gen != s_wlan_state_gen)
        return;
    edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_STABLE);
    s_wlan_stable_arm = RT_FALSE;
    rt_kprintf("[wifi] M33 wifi_state=STABLE (%u ms)\n",
               (unsigned)EDGI_ALARM_UPLOAD_DELAY_AFTER_READY_MS);
#if EDGI_ALARM_UPLOAD_AUTO_ON_WIFI_READY
    edgi_alarm_set_auto_upload(1);
    rt_kprintf("[wifi] alarm_upload ON (auto)\n");
#else
    rt_kprintf("[wifi] alarm_upload OFF — 确认 KEY/绿灯正常后: alarm_upload 1\n");
#endif
}

static void edgi_wlan_arm_stable_delay(void)
{
    rt_thread_t t;
    rt_uint32_t gen;

    if (s_wlan_stable_arm)
        return;

    s_wlan_stable_arm = RT_TRUE;
    gen = ++s_wlan_state_gen;
    rt_kprintf("[wifi] connected: %u ms later wifi_state=STABLE\n",
               (unsigned)EDGI_ALARM_UPLOAD_DELAY_AFTER_READY_MS);
    t = rt_thread_create("alm_arm", edgi_alarm_upload_arm_delayed,
                         (void *)(uintptr_t)gen, 1024, 25, 10);
    if (t != RT_NULL)
        rt_thread_startup(t);
    else
    {
        s_wlan_stable_arm = RT_FALSE;
        edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_STABLE);
    }
}

static void edgi_wlan_alarm_event(int event, struct rt_wlan_buff *buff, void *parameter)
{
    (void)buff;
    (void)parameter;

    if (event == RT_WLAN_EVT_SCAN_DONE)
    {
        if (!rt_wlan_is_connected())
            edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_JOIN);
    }
    else if (event == RT_WLAN_EVT_STA_CONNECTED)
    {
        edgi_pc_box_finish(EDGI_WIFI_PC_CONNECTED, 0);
        edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_READY);
        edgi_wlan_arm_stable_delay();
    }
    else if (event == RT_WLAN_EVT_READY)
    {
        edgi_pc_box_finish(EDGI_WIFI_PC_CONNECTED, 0);
        edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_READY);
        edgi_wlan_arm_stable_delay();
        rt_kprintf("[wifi] ready: %u ms 后 wifi_state=STABLE\n",
                   (unsigned)EDGI_ALARM_UPLOAD_DELAY_AFTER_READY_MS);
    }
    else if (event == RT_WLAN_EVT_STA_DISCONNECTED ||
             event == RT_WLAN_EVT_STA_CONNECTED_FAIL)
    {
        if (event == RT_WLAN_EVT_STA_CONNECTED_FAIL)
            edgi_pc_box_finish(EDGI_WIFI_PC_FAILED, -RT_ERROR);
        s_wlan_state_gen++;
        s_wlan_stable_arm = RT_FALSE;
        edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_IDLE);
        rt_kprintf("[wifi] offline: M33 alarm auto upload paused\n");
        edgi_alarm_set_auto_upload(0);
    }
}

static void edgi_wlan_register_alarm_events(void)
{
    if (s_wlan_alarm_events_registered)
        return;

    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, edgi_wlan_alarm_event, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED, edgi_wlan_alarm_event, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_DONE, edgi_wlan_alarm_event, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, edgi_wlan_alarm_event, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_CONNECTED_FAIL, edgi_wlan_alarm_event, RT_NULL);
    s_wlan_alarm_events_registered = RT_TRUE;
}

#if EDGI_WIFI_FLASH_PERSIST
static int edgi_wlan_cfg_read(void *buff, int len)
{
    if (s_wlan_part == RT_NULL || buff == RT_NULL || len <= 0)
        return -1;
    if (fal_partition_read(s_wlan_part, 0, buff, (size_t)len) < 0)
        return -1;
    return len;
}

static int edgi_wlan_cfg_get_len(void)
{
    uint32_t header[2];
    uint32_t stored_len;

    if (s_wlan_part == RT_NULL)
        return -1;
    if (fal_partition_read(s_wlan_part, 0, (uint8_t *)header, sizeof(header)) < 0)
        return -1;

    stored_len = header[1];
    if (stored_len < 16u || stored_len > EDGI_WLAN_CFG_MAXLEN)
        return -1;
    return (int)stored_len;
}

static int edgi_wlan_cfg_write(void *buff, int len)
{
    if (s_wlan_part == RT_NULL || buff == RT_NULL || len <= 0 || len > EDGI_WLAN_CFG_MAXLEN)
        return -1;
    if (fal_partition_erase(s_wlan_part, 0, s_wlan_part->len) < 0)
        return -1;
    if (fal_partition_write(s_wlan_part, 0, buff, (size_t)len) < 0)
        return -1;
    return len;
}

static const struct rt_wlan_cfg_ops s_edgi_wlan_cfg_ops =
{
    .read_cfg  = edgi_wlan_cfg_read,
    .get_len   = edgi_wlan_cfg_get_len,
    .write_cfg = edgi_wlan_cfg_write,
};

static rt_bool_t edgi_reset_is_hw_button(uint32_t reason)
{
    return (reason & (CY_SYSLIB_RESET_XRES | CY_SYSLIB_RESET_PXRES |
                      CY_SYSLIB_RESET_STRUCT_XRES)) ? RT_TRUE : RT_FALSE;
}
#endif

static int edgi_wlan_prepare_common(rt_bool_t autoreconnect)
{
    edgi_wlan_register_alarm_events();

#if !EDGI_WIFI_FLASH_PERSIST
    (void)autoreconnect;
    s_wlan_part = RT_NULL;
    rt_wlan_config_autoreconnect(RT_FALSE);
    rt_kprintf("[wifi] flash persist OFF; use wifi_join_edgi each boot\n");
    return 0;
#else
    s_wlan_part = fal_partition_find(EDGI_WLAN_CFG_PART);
    if (s_wlan_part == RT_NULL)
    {
        rt_kprintf("[wifi] wlan_cfg partition missing, no flash persist\n");
        return 0;
    }

    rt_wlan_cfg_set_ops(&s_edgi_wlan_cfg_ops);

    if (autoreconnect)
    {
        uint32_t reason = Cy_SysLib_GetResetReason();
        Cy_SysLib_ClearResetReason();

        if (EDGI_WIFI_CLEAR_CFG_ON_XRES && edgi_reset_is_hw_button(reason))
        {
            rt_wlan_cfg_delete_all();
            fal_partition_erase(s_wlan_part, 0, s_wlan_part->len);
            rt_wlan_config_autoreconnect(RT_FALSE);
            rt_kprintf("[wifi] HW RESET: flash cfg cleared, please join WiFi again\n");
            return 0;
        }
    }

    if (rt_wlan_cfg_cache_refresh() != RT_EOK)
    {
        rt_kprintf("[wifi] no saved WiFi in flash\n");
    }
    else
    {
        rt_kprintf("[wifi] loaded %d WiFi profile(s) from flash\n", rt_wlan_cfg_get_num());
    }

    rt_wlan_config_autoreconnect(autoreconnect);
    return 0;
#endif
}

int edgi_wlan_prepare_manual(void)
{
    return edgi_wlan_prepare_common(RT_TRUE);
}

int edgi_wlan_prepare_auto(void)
{
    return edgi_wlan_prepare_common(RT_TRUE);
}

#ifdef RT_USING_FINSH
static rt_err_t edgi_wifi_join_request(const char *ssid, const char *key)
{
    rt_err_t e;

    edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_JOIN);
    rt_kprintf("[M55] wifi join: ssid=%s ...\n", ssid);
    e = rt_wlan_connect(ssid, key);
    if (e == RT_EOK)
    {
        rt_kprintf("[M55] join requested (wait wifi ready event)\n");
        return RT_EOK;
    }

    edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_JOIN_FAIL);
    rt_kprintf("[M55] join request failed (%d)\n", (int)e);
    return e;
}

#include <finsh.h>
static void edgi_wifi_join_edgi_cmd(int argc, char **argv)
{
    const char *ssid = EDGI_WIFI_DEFAULT_SSID;
    const char *key  = EDGI_WIFI_DEFAULT_KEY;

    /* wifi_join_edgi <密码>  或  wifi_join_edgi <ssid> <密码> */
    if (argc == 2)
        key = argv[1];
    else if (argc >= 3)
    {
        ssid = argv[1];
        key  = argv[2];
    }

    (void)edgi_wifi_join_request(ssid, key);
}

MSH_CMD_EXPORT_ALIAS(edgi_wifi_join_edgi_cmd, wifi_join_edgi,
                     join: wifi_join_edgi <pwd>  or  wifi_join_edgi <ssid> <pwd>);
#endif

#if EDGI_WIFI_AUTO_JOIN
static void edgi_wifi_auto_join_entry(void *param)
{
    int saved_profiles;
    rt_bool_t connected_seen = RT_FALSE;

    RT_UNUSED(param);

    edgi_pc_box_init();
    edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_AUTO_DELAY);
    rt_thread_mdelay(EDGI_WIFI_AUTO_JOIN_DELAY_MS);
    edgi_wlan_prepare_auto();
    saved_profiles = rt_wlan_cfg_get_num();
    rt_kprintf("[wifi] auto: start driver after %u ms\n",
               (unsigned)EDGI_WIFI_AUTO_JOIN_DELAY_MS);
    edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_STARTING);
    if (edgi_wifi_start_auto() != RT_EOK)
        edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_START_FAIL);
    rt_thread_mdelay(EDGI_WIFI_DRIVER_SETTLE_MS);

    rt_kprintf("[wifi] boot: %d saved profile(s); PC provisioning always available\n",
               saved_profiles);
    while (1)
    {
        if (rt_wlan_is_connected())
        {
            edgi_pc_box_finish(EDGI_WIFI_PC_CONNECTED, 0);
            if (!connected_seen)
            {
                connected_seen = RT_TRUE;
                edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_READY);
                edgi_wlan_arm_stable_delay();
            }
        }
        else if (s_pc_active_seq == 0u)
        {
            connected_seen = RT_FALSE;
            edgi_m55_shm_set_wifi_state(
                saved_profiles > 0 ? EDGI_WIFI_SHM_RECONNECT : EDGI_WIFI_SHM_PC_WAIT);
        }

        edgi_pc_box_poll();
        if (rt_wlan_cfg_get_num() > saved_profiles)
            saved_profiles = rt_wlan_cfg_get_num();
        rt_thread_mdelay(250);
    }
}
#endif

static int edgi_wlan_boot_init(void)
{
#if EDGI_WIFI_AUTO_JOIN
    edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_AUTO_ARMED);
    rt_thread_t t = rt_thread_create("wifi_auto",
                                     edgi_wifi_auto_join_entry,
                                     RT_NULL,
                                     3072,
                                     25,
                                     20);
    if (t == RT_NULL)
    {
        edgi_m55_shm_set_wifi_state(EDGI_WIFI_SHM_THREAD_FAIL);
        return -1;
    }
    rt_thread_startup(t);
    rt_kprintf("[wifi] saved-profile auto connect + PC provisioning armed, delay=%u ms\n",
               (unsigned)EDGI_WIFI_AUTO_JOIN_DELAY_MS);
    return 0;
#else
#if !EDGI_WIFI_AUTO_START
    rt_kprintf("[wifi] early auto start disabled; flash persist off\n");
    return 0;
#else
    return edgi_wlan_prepare_common(RT_TRUE);
#endif
#endif
}
INIT_APP_EXPORT(edgi_wlan_boot_init);
