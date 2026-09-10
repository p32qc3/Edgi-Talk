#include "../include/edgi_audio_capture.h"
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

/*
 * Edgi-Talk BSP：PDM 已由 HAL drv_pdm.c 注册为音频设备 "mic0"。
 * 本文件为团队“drv_pdm”实现，头文件必须用 edgi_audio_capture.h，避免与 HAL drv_pdm.h 同名冲突。
 */

static rt_device_t s_mic;
static rt_device_t s_sound;
static rt_uint8_t  s_use_stub;
static volatile rt_bool_t s_capture_paused;

static void fill_stub(int16_t *out, rt_size_t n)
{
    static rt_uint32_t ph;
    for (rt_size_t i = 0; i < n; i++)
    {
        int32_t s = (int32_t)((ph & 0xFF) * 4 - 512);
        ph++;
        out[i] = (int16_t)(s + (int32_t)((ph & 7) * 20));
    }
}

static rt_err_t open_sound_clock(void)
{
    struct rt_audio_caps caps;

    s_sound = rt_device_find("sound0");
    if (s_sound == RT_NULL)
        return RT_EOK;

    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type  = AUDIO_MIXER_VOLUME;
    caps.udata.value = 5;
    if (rt_device_control(s_sound, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
        return -RT_ERROR;

    return rt_device_open(s_sound, RT_DEVICE_OFLAG_WRONLY);
}

rt_err_t edgi_audio_capture_init(void)
{
    s_mic = rt_device_find("mic0");
    if (s_mic == RT_NULL)
    {
        s_use_stub = 1;
        rt_kprintf("[AI] mic0 missing, synthetic audio fallback\r\n");
        return RT_EOK;
    }

    (void)open_sound_clock();

    struct rt_audio_caps caps;
    caps.main_type = AUDIO_TYPE_INPUT;
    caps.sub_type  = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = (int)EDGI_AUDIO_SAMPLE_RATE_HZ;
    caps.udata.config.channels   = 1;
    caps.udata.config.samplebits = 16;
    if (rt_device_control(s_mic, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
    {
        caps.udata.config.channels = 2;
        rt_device_control(s_mic, AUDIO_CTL_CONFIGURE, &caps);
    }

    caps.main_type = AUDIO_TYPE_MIXER;
    caps.sub_type  = AUDIO_MIXER_VOLUME;
    caps.udata.value = EDGI_AUDIO_MIC_VOLUME;
    rt_device_control(s_mic, AUDIO_CTL_CONFIGURE, &caps);

    if (rt_device_open(s_mic, RT_DEVICE_OFLAG_RDONLY) != RT_EOK)
    {
        s_mic = RT_NULL;
        s_use_stub = 1;
        rt_kprintf("[AI] mic0 open failed, synthetic audio fallback\r\n");
        return RT_EOK;
    }

    s_use_stub = 0;
    rt_kprintf("[AI] mic0 capture ready\r\n");
    return RT_EOK;
}

void edgi_audio_capture_set_paused(rt_bool_t paused)
{
    s_capture_paused = paused;
}

rt_bool_t edgi_audio_capture_is_paused(void)
{
    return s_capture_paused;
}

rt_ssize_t edgi_audio_capture_read_frame(int16_t *buf, rt_size_t samples)
{
    if (buf == RT_NULL || samples < EDGI_AUDIO_SAMPLES_PER_FRAME)
        return -RT_EINVAL;

    if (s_capture_paused)
    {
        rt_memset(buf, 0, EDGI_AUDIO_SAMPLES_PER_FRAME * sizeof(int16_t));
        rt_thread_mdelay(EDGI_AUDIO_FRAME_MS);
        return (rt_ssize_t)EDGI_AUDIO_SAMPLES_PER_FRAME;
    }

    if (s_use_stub || s_mic == RT_NULL)
    {
        fill_stub(buf, EDGI_AUDIO_SAMPLES_PER_FRAME);
        return (rt_ssize_t)EDGI_AUDIO_SAMPLES_PER_FRAME;
    }

    rt_size_t need = EDGI_AUDIO_SAMPLES_PER_FRAME * sizeof(int16_t);
    rt_size_t got  = 0;
    rt_uint8_t *p  = (rt_uint8_t *)buf;

    /*
     * mic0 若长期无数据，原实现会死循环。超时后返回错误，AI 线程可降级或重试。
     */
    int spins = 0;
    while (got < need)
    {
        rt_ssize_t n = rt_device_read(s_mic, 0, p + got, need - got);
        if (n <= 0)
        {
            rt_thread_mdelay(1);
            if (++spins > 2000)
                return -RT_ETIMEOUT;
            continue;
        }
        spins = 0;
        got += (rt_size_t)n;
    }

    return (rt_ssize_t)EDGI_AUDIO_SAMPLES_PER_FRAME;
}

rt_err_t edgi_audio_playback_start(void)
{
    struct rt_audio_caps caps;
    int stream = AUDIO_STREAM_REPLAY;
    if (s_sound == RT_NULL) return -RT_ENOSYS;

    caps.main_type = AUDIO_TYPE_OUTPUT;
    caps.sub_type = AUDIO_DSP_PARAM;
    caps.udata.config.samplerate = (int)EDGI_AUDIO_SAMPLE_RATE_HZ;
    caps.udata.config.channels = 1;
    caps.udata.config.samplebits = 16;
    if (rt_device_control(s_sound, AUDIO_CTL_CONFIGURE, &caps) != RT_EOK)
        return -RT_ERROR;
    return rt_device_control(s_sound, AUDIO_CTL_START, &stream);
}

rt_ssize_t edgi_audio_playback_write(const void *data, rt_size_t bytes)
{
    if (s_sound == RT_NULL || data == RT_NULL || !bytes) return -RT_EINVAL;
    return rt_device_write(s_sound, 0, data, bytes);
}

void edgi_audio_playback_stop(void)
{
    int stream = AUDIO_STREAM_REPLAY;
    if (s_sound != RT_NULL)
        (void)rt_device_control(s_sound, AUDIO_CTL_STOP, &stream);
}
