#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <lv_rt_thread_conf.h>
#include "vg_lite.h"
#include "vg_lite_platform.h"
#include "lv_port_disp.h"
#include "pet_app.h"
#include "pet_imu.h"

#define LED_PIN_G               GET_PIN(16, 6)

void lv_user_gui_init(void)
{
    int result = pet_app_init();
    if (result != RT_EOK) rt_kprintf("[A1] pet app init failed: %d\n", result);
}

int main(void)
{
    rt_kprintf("Hello RT-Thread\n");
    rt_kprintf("It's cortex-m55\n");
    rt_pin_mode(LED_PIN_G, PIN_MODE_OUTPUT);
    if (pet_imu_init() != RT_EOK) rt_kprintf("[A3] IMU worker failed to start\n");
    lvgl_thread_init();

    while (1)
    {
        rt_pin_write(LED_PIN_G, PIN_LOW);
        rt_thread_mdelay(500);
        rt_pin_write(LED_PIN_G, PIN_HIGH);
        rt_thread_mdelay(500);
    }
    return 0;
}
