#include <rtthread.h>
#include "m33/include/edgi_key_button.h"

int main(void)
{
    rt_kprintf("\r\n======== Edgi-Talk_Audio CM33 ========\r\n");
    rt_kprintf("KEY/blue on CM33 | cmd: cm55_stat m33_stat m33_fire\r\n");
    rt_kprintf("Screen/LVGL on M55 | M55 does not use COM5 input\r\n");

    edgi_key_button_init();
    rt_kprintf("KEY: boot init finished (try long press ~1s)\r\n");

    while (1)
    {
        rt_thread_mdelay(1000);
    }
}
