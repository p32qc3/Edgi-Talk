#include "voice_wifi.h"

#include "voice_config.h"

#include <rtthread.h>
#include <wlan_mgnt.h>
#include <lwip/ip_addr.h>
#include <netdev.h>
#include <stdint.h>

extern int edgi_wifi_start_auto(void);

static volatile uint8_t s_wifi_driver_state;
static rt_thread_t s_wifi_thread;

/* The board Wi-Fi driver reports progress through this existing hook. */
void edgi_m55_shm_set_wifi_state(uint8_t state)
{
    s_wifi_driver_state = state;
}

int edgi_wlan_prepare_auto(void)
{
    rt_wlan_config_autoreconnect(RT_TRUE);
    return RT_EOK;
}

int edgi_wlan_prepare_manual(void)
{
    rt_wlan_config_autoreconnect(RT_TRUE);
    return RT_EOK;
}

static void wifi_entry(void *parameter)
{
    (void)parameter;
    rt_thread_mdelay(3000);
    rt_wlan_config_autoreconnect(RT_TRUE);
    if (edgi_wifi_start_auto() != RT_EOK)
    {
        rt_kprintf("[voice] WiFi driver start failed\n");
        return;
    }
    rt_thread_mdelay(3000);
    if (rt_wlan_connect(VOICE_WIFI_SSID,
                        VOICE_WIFI_PASSWORD[0] ? VOICE_WIFI_PASSWORD : RT_NULL)
        != RT_EOK)
    {
        rt_kprintf("[voice] WiFi join request failed\n");
        return;
    }
    rt_kprintf("[voice] WiFi join requested: %s\n", VOICE_WIFI_SSID);
}

int voice_wifi_start(void)
{
    if (!voice_config_is_ready()) return 0;
    if (s_wifi_thread) return 1;
    s_wifi_thread = rt_thread_create("voicewifi", wifi_entry, RT_NULL,
                                     3072, 23, 20);
    if (!s_wifi_thread) return 0;
    rt_thread_startup(s_wifi_thread);
    return 1;
}

int voice_wifi_is_online(void)
{
    struct netdev *device;
    if (!rt_wlan_is_connected()) return 0;
    device = netdev_get_by_name("w0");
    if (!device) device = netdev_get_by_name("wlan0");
    return device && netdev_is_up(device) && netdev_is_link_up(device);
}
