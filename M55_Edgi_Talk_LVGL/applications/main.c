#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "edgi_m33_m55_shm.h"

#ifndef EDGI_M55_UART_CONSOLE
#define EDGI_M55_UART_CONSOLE 1
#endif

#if defined(RT_USING_FINSH) && EDGI_M55_UART_CONSOLE
#include <finsh.h>
#endif

extern int lvgl_thread_init(void);

#if defined(RT_USING_FINSH) && EDGI_M55_UART_CONSOLE
static void edgi_m55_console_release_entry(void *param)
{
    RT_UNUSED(param);
    /* 等 finsh 线程完成 uart2 打开后再释放 RX，交给 M33 debug 口 */
    rt_thread_mdelay(1500);
    finsh_release_device();
    rt_kprintf("\r\n[M55] DEBUG UART RX 已释放 -> 请在 M33 串口(COM)输入 msh 命令\r\n");
    rt_kprintf("[M55] 本核仍可通过日志输出；屏/LVGL 命令需 M33 侧 cm55_stat 等\r\n");
}
#endif

int main(void)
{
    edgi_m55_shm_set_ui_state(EDGI_M55_UI_MAIN);
    rt_kprintf("[M55] main() entered — 若从未见过此行，CM55 未跑或固件未烧到 0x60580400\n");
    rt_kprintf("[M55] Minimal test program started\n");
    rt_kprintf("[M55] System clock: %d Hz\n", SystemCoreClock);
    rt_kprintf("[M55] RT-Thread version: %d.%d.%d\n",
               RT_VERSION_MAJOR, RT_VERSION_MINOR, RT_VERSION_PATCH);

    // 测试基本功能
    rt_kprintf("[M55] Testing GPIO...\n");
    // 测试GPIO输出

    rt_kprintf("[M55] Testing memory...\n");
    // 测试内存分配
    void *test_mem = rt_malloc(1024);
    if (test_mem) {
        rt_kprintf("[M55] Memory allocation successful: %p\n", test_mem);
        rt_free(test_mem);
        rt_kprintf("[M55] Memory free successful\n");
    } else {
        rt_kprintf("[M55] Memory allocation failed\n");
    }

    rt_kprintf("[M55] Minimal test completed successfully\n");

#if defined(RT_USING_FINSH) && EDGI_M55_UART_CONSOLE
    {
        rt_thread_t tr = rt_thread_create("consrel", edgi_m55_console_release_entry, RT_NULL,
                                          1024, 25, 10);
        if (tr != RT_NULL)
            rt_thread_startup(tr);
    }
#endif

    if (lvgl_thread_init() == 0)
    {
        edgi_m55_shm_set_ui_state(EDGI_M55_UI_LVGL_THREAD);
        rt_kprintf("[M55] LVGL thread started (屏不亮请看 init screen success / 执行 disp_on lcd_test)\n");
    }
    else
    {
        edgi_m55_shm_set_ui_state(EDGI_M55_UI_LVGL_FAIL);
        rt_kprintf("[M55] LVGL thread start failed — 执行 msh: lvgl_thread_init\n");
    }

    /* Main idle: LVGL runs in its own thread; avoid periodic uart spam here */
    while (1)
    {
        rt_thread_mdelay(1000);
    }
}
