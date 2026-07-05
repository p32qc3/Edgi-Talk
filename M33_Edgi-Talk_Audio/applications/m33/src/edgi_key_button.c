/*
 * KEY (P8.3 / CYBSP_SW2): 短按=暂停采音+蓝灯；长按=软关机，再按 KEY 重启。
 */
#include "../include/edgi_key_button.h"
#include "../include/edgi_audio_capture.h"
#include "../include/edgi_led_green.h"
#include "../include/edgi_m33_m55_shm.h"
#include "board.h"
#include "cy_gpio.h"
#include "cybsp.h"
#include "gpio_pse84_bga_220.h"
#include <rtdevice.h>
#include <rtthread.h>
#include <stdint.h>

#define EDGI_LED_BLUE_PIN       GET_PIN(16, 5)
#define EDGI_KEY_POLL_MS        20
#define EDGI_KEY_DEBOUNCE_MS    50
#define EDGI_KEY_DEBOUNCE_CNT   ((EDGI_KEY_DEBOUNCE_MS + EDGI_KEY_POLL_MS - 1) / EDGI_KEY_POLL_MS)
#define EDGI_KEY_LONG_MS        1000
/* M55 LVGL/屏初始化后可能改 P16.5 复用，周期性拉回 GPIO 才能保持蓝灯 */
#define EDGI_LED_REFRESH_MS     1000
#define EDGI_KEY_GPIO_REFRESH_MS 3000
#define EDGI_KEY_WIFI_CHECK_MS   500
/* 上电后忽略长按，避免误触关机（key_dbg/插线抖动） */
#define EDGI_KEY_LONG_GUARD_MS  5000
#define EDGI_KEY_THREAD_PRIO    5

#ifndef EDGI_KEY_ACTIVE_LOW
#define EDGI_KEY_ACTIVE_LOW     1
#endif

#ifndef EDGI_KEY_EDGE_LOG
#define EDGI_KEY_EDGE_LOG       0
#endif

#ifndef EDGI_LED_GREEN_PIN
#define EDGI_LED_GREEN_PIN      GET_PIN(16, 6)
#endif

#ifndef EDGI_GREEN_BLINK_MS
#define EDGI_GREEN_BLINK_MS     500
#endif

static volatile rt_bool_t s_recording = RT_TRUE;
static volatile rt_bool_t s_shutting_down = RT_FALSE;
static rt_thread_t s_key_thread = RT_NULL;
static rt_bool_t s_green_on;
static rt_bool_t s_green_started;

static void edgi_led_green_hw(rt_bool_t on)
{
#if defined(CYBSP_LED_GREEN_ENABLED) && (CYBSP_LED_GREEN_ENABLED)
    uint32_t level = on ? 1u : 0u;

    Cy_GPIO_Pin_FastInit(CYBSP_LED_GREEN_PORT, CYBSP_LED_GREEN_PIN,
                         CY_GPIO_DM_STRONG_IN_OFF, level, P16_6_GPIO);
#else
    rt_pin_mode(EDGI_LED_GREEN_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(EDGI_LED_GREEN_PIN, on ? PIN_HIGH : PIN_LOW);
#endif
}

void edgi_led_green_set(rt_bool_t on)
{
    s_green_on = on ? RT_TRUE : RT_FALSE;
    edgi_led_green_hw(s_green_on);
}

void edgi_led_green_blink_start(void)
{
    if (s_green_started)
        return;
    s_green_started = RT_TRUE;
    s_green_on = RT_TRUE;
    edgi_led_green_hw(s_green_on);
}

static void edgi_key_gpio_init(void)
{
#if defined(CYBSP_SW2_ENABLED) && (CYBSP_SW2_ENABLED)
    Cy_GPIO_Pin_FastInit(CYBSP_SW2_PORT, CYBSP_SW2_PIN,
                         CY_GPIO_DM_PULLUP, 1u, P8_3_GPIO);
#endif
}

void edgi_key_gpio_reinit(void)
{
    edgi_key_gpio_init();
    rt_kprintf("[CM33] KEY P8.3 reinit (CM55 已启动后若按键无效试此)\n");
}

static rt_bool_t edgi_key_gpio_pressed(void)
{
    uint32_t v = Cy_GPIO_Read(CYBSP_SW2_PORT, CYBSP_SW2_PIN);

#if EDGI_KEY_ACTIVE_LOW
    return (v == 0u) ? RT_TRUE : RT_FALSE;
#else
    return (v != 0u) ? RT_TRUE : RT_FALSE;
#endif
}

static void edgi_led_blue_set(rt_bool_t on)
{
#if defined(CYBSP_LED_BLUE_ENABLED) && (CYBSP_LED_BLUE_ENABLED)
    uint32_t level = on ? 1u : 0u;

    /* 必须用 GPIO 功能号；M55 跑显示后 rt_pin 可能失效 */
    Cy_GPIO_Pin_FastInit(CYBSP_LED_BLUE_PORT, CYBSP_LED_BLUE_PIN,
                         CY_GPIO_DM_STRONG_IN_OFF, level, P16_5_GPIO);
#else
    rt_pin_mode(EDGI_LED_BLUE_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(EDGI_LED_BLUE_PIN, on ? PIN_HIGH : PIN_LOW);
#endif
}

static void edgi_key_on_short(void)
{
    if (s_shutting_down)
        return;

    s_recording = RT_TRUE;
    edgi_m33_service_set_enable(1u);
    edgi_led_blue_set(RT_TRUE);
    edgi_audio_capture_set_paused(RT_FALSE);
    rt_kprintf("\r\n[CM33] KEY: short -> record on\r\n");
}

static void edgi_key_on_long(void)
{
    if (s_shutting_down)
        return;

    s_shutting_down = RT_TRUE;
    edgi_m33_service_set_enable(0u);
    rt_kprintf("\r\n[CM33] KEY: long -> soft poweroff (M55 也会停, 再按 KEY 重启)\r\n");
    poweroff();
}

static void edgi_key_poll_entry(void *param)
{
    rt_bool_t last_raw = edgi_key_gpio_pressed();
    rt_bool_t stable_pressed = last_raw;
    rt_bool_t long_done = RT_FALSE;
    rt_uint8_t stable_cnt = 1;
    rt_tick_t press_tick = rt_tick_get();
    rt_tick_t long_ticks = rt_tick_from_millisecond(EDGI_KEY_LONG_MS);
    rt_tick_t guard_until = rt_tick_get() + rt_tick_from_millisecond(EDGI_KEY_LONG_GUARD_MS);
    rt_tick_t last_led_refresh = rt_tick_get();
    rt_tick_t last_key_gpio_refresh = rt_tick_get();
    rt_tick_t last_green_blink = rt_tick_get();
    rt_tick_t last_wifi_check = rt_tick_get();
    uint8_t last_wifi_st = 0xFFu;

    (void)param;

    edgi_led_green_blink_start();

    while (1)
    {
        rt_bool_t raw = edgi_key_gpio_pressed();
        rt_tick_t now = rt_tick_get();

        if ((now - last_green_blink) >= rt_tick_from_millisecond(EDGI_GREEN_BLINK_MS))
        {
            s_green_on = s_green_on ? RT_FALSE : RT_TRUE;
            edgi_led_green_hw(s_green_on);
            last_green_blink = now;
        }

        if ((now - last_wifi_check) >= rt_tick_from_millisecond(EDGI_KEY_WIFI_CHECK_MS))
        {
            uint8_t ws = edgi_m33_shm_wifi_state_read();

            if (ws != last_wifi_st)
            {
                if (ws >= EDGI_WIFI_SHM_JOIN && ws < EDGI_WIFI_SHM_STABLE)
                    edgi_key_gpio_reinit();
                last_wifi_st = ws;
            }
            if (ws >= EDGI_WIFI_SHM_JOIN && ws < EDGI_WIFI_SHM_STABLE)
                edgi_key_gpio_init();
            last_wifi_check = now;
        }
        if ((now - last_key_gpio_refresh) >= rt_tick_from_millisecond(EDGI_KEY_GPIO_REFRESH_MS))
        {
            edgi_key_gpio_init();
            last_key_gpio_refresh = now;
        }

        if (s_recording && (now - last_led_refresh) >= rt_tick_from_millisecond(EDGI_LED_REFRESH_MS))
        {
            edgi_led_blue_set(RT_TRUE);
            last_led_refresh = now;
        }

        if (raw == last_raw)
        {
            if (stable_cnt < 255)
                stable_cnt++;
        }
        else
        {
            last_raw = raw;
            stable_cnt = 1;
        }

        if (stable_cnt >= EDGI_KEY_DEBOUNCE_CNT && raw != stable_pressed)
        {
            stable_pressed = raw;

            if (stable_pressed)
            {
                long_done = RT_FALSE;
                press_tick = now;
#if EDGI_KEY_EDGE_LOG
                rt_kprintf("[CM33] KEY: down\r\n");
#endif
            }
            else
            {
#if EDGI_KEY_EDGE_LOG
                rt_kprintf("[CM33] KEY: up\r\n");
#endif
                if (!long_done)
                    edgi_key_on_short();
                long_done = RT_FALSE;
            }
        }

        if (stable_pressed && !long_done && !s_shutting_down && (now >= guard_until))
        {
            if ((now - press_tick) >= long_ticks)
            {
                long_done = RT_TRUE;
                edgi_key_on_long();
            }
        }

        rt_thread_mdelay(EDGI_KEY_POLL_MS);
    }
}

void edgi_key_button_init(void)
{
    uint32_t raw;

    edgi_key_gpio_init();
    raw = Cy_GPIO_Read(CYBSP_SW2_PORT, CYBSP_SW2_PIN);

    edgi_m33_service_set_enable(1);
    s_recording = RT_TRUE;
    s_shutting_down = RT_FALSE;
    edgi_led_blue_set(RT_TRUE);
    edgi_audio_capture_set_paused(RT_FALSE);

    rt_kprintf("\r\nKEY: init P8.3 raw=%u %s | 蓝灯=采集中 短按=暂停(灭蓝灯) 长按=%ums关机\r\n",
               (unsigned)raw, edgi_key_gpio_pressed() ? "PRESSED" : "idle",
               (unsigned)EDGI_KEY_LONG_MS);

    if (s_key_thread != RT_NULL)
        return;

    s_key_thread = rt_thread_create("edgi_key", edgi_key_poll_entry, RT_NULL,
                                    3072, EDGI_KEY_THREAD_PRIO, 10);
    if (s_key_thread != RT_NULL)
    {
        rt_thread_startup(s_key_thread);
        rt_kprintf("KEY: poll thread started\n");
    }
    else
    {
        rt_kprintf("KEY: thread create failed\n");
    }
}

rt_bool_t edgi_key_recording_enabled(void)
{
    return s_recording;
}

void edgi_key_shm_remote_exec(uint8_t op)
{
    int i;

    if (op == EDGI_SHM_CMD_KEY_DBG)
    {
        rt_kprintf("KEY dbg (via M55 cm33): 8 x %dms (0=release 1=press)\n", EDGI_KEY_POLL_MS);
        for (i = 0; i < 8; i++)
        {
            uint32_t v = Cy_GPIO_Read(CYBSP_SW2_PORT, CYBSP_SW2_PIN);
            rt_kprintf("  [%03d] raw=%u pressed=%d\n", i, (unsigned)v, edgi_key_gpio_pressed());
            rt_thread_mdelay(EDGI_KEY_POLL_MS);
        }
        return;
    }
    if (op == EDGI_SHM_CMD_KEY_REC)
    {
        s_recording = RT_TRUE;
        edgi_led_blue_set(RT_TRUE);
        edgi_audio_capture_set_paused(RT_FALSE);
        rt_kprintf("KEY: force record ON (blue LED, via M55 cm33)\n");
    }
}

#ifdef RT_USING_MSH
#include <finsh.h>
#include <stdlib.h>

static void key_dbg(int argc, char **argv)
{
    int n = 20;
    int i;

    if (argc >= 2)
        n = atoi(argv[1]);
    if (n < 1)
        n = 1;
    if (n > 200)
        n = 200;

    rt_kprintf("KEY dbg: sample %d x %dms (0=release 1=press on P8.3)\n", n, EDGI_KEY_POLL_MS);
    for (i = 0; i < n; i++)
    {
        uint32_t v = Cy_GPIO_Read(CYBSP_SW2_PORT, CYBSP_SW2_PIN);
        rt_kprintf("  [%03d] raw=%u pressed=%d\n", i, (unsigned)v, edgi_key_gpio_pressed());
        rt_thread_mdelay(EDGI_KEY_POLL_MS);
    }
}
MSH_CMD_EXPORT(key_dbg, KEY P8.3 debug: key_dbg [samples]);

static void key_off(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("KEY: key_off -> CTRL/屏背光断电，需按 KEY 或硬复位恢复\n");
    edgi_key_on_long();
}
MSH_CMD_EXPORT(key_off, Test soft poweroff (same as long press));

static void key_rec(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    s_recording = RT_TRUE;
    edgi_led_blue_set(RT_TRUE);
    edgi_audio_capture_set_paused(RT_FALSE);
    rt_kprintf("KEY: force record ON (blue LED)\n");
}
MSH_CMD_EXPORT(key_rec, Force recording on + blue LED);

static void key_stat(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    rt_kprintf("KEY: recording=%d blue_should=%s raw=%u pressed=%d\n",
               s_recording ? 1 : 0,
               s_recording ? "ON" : "OFF",
               (unsigned)Cy_GPIO_Read(CYBSP_SW2_PORT, CYBSP_SW2_PIN),
               edgi_key_gpio_pressed());
}
MSH_CMD_EXPORT(key_stat, KEY/blue LED status);
#endif
