#include <rtthread.h>

#if defined(RT_USING_FINSH) && defined(RT_USING_DEVICE)
#include <finsh.h>
#include <shell.h>
#include <rtdevice.h>
#endif

void edgi_m33_console_reclaim(void)
{
#if defined(RT_USING_FINSH) && defined(RT_USING_DEVICE)
    rt_device_t dev;

    finsh_reopen_device();

    dev = rt_device_find(RT_CONSOLE_DEVICE_NAME);
    if (dev != RT_NULL)
    {
        rt_device_control(dev, RT_DEVICE_CTRL_SET_INT, RT_NULL);
    }

    rt_kprintf("\r\n[CM33] msh input reclaimed: cm55_stat / m33_stat / m33_fire\r\n");
    rt_kprintf(FINSH_PROMPT);
#endif
}

#ifdef RT_USING_MSH
static void console_fix(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);
    edgi_m33_console_reclaim();
}
MSH_CMD_EXPORT(console_fix, Reclaim uart2 RX for M33 msh after M55 boot);
#endif
