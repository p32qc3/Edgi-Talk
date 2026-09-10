#ifndef EDGI_AUDIO_CAPTURE_H
#define EDGI_AUDIO_CAPTURE_H

#include <rtthread.h>
#include <stdint.h>

/**
 * 应用层采音 API（团队 drv_pdm 职责）。
 * 注意：勿命名为 drv_pdm.h，会与 BSP 中 HAL_Drivers/drv_pdm.h 重名导致错包含。
 *
 * - 16 kHz, 16-bit PCM, mono
 * - 每帧 20 ms => 320 样本；BSP mic0 约 10ms 一包，本模块读满 320 点。
 * BSP 注册设备名 "mic0"，见 HAL_Drivers/drv_pdm.c。
 */
#define EDGI_AUDIO_SAMPLE_RATE_HZ   16000u
#define EDGI_AUDIO_FRAME_MS         20u
#define EDGI_AUDIO_SAMPLES_PER_FRAME \
    ((EDGI_AUDIO_SAMPLE_RATE_HZ * EDGI_AUDIO_FRAME_MS) / 1000u)

/* 频谱能量右移位数；10 时演示 wav 易全 255→特征全 1.0 无法区分四类 */
#ifndef EDGI_SPEC_ENERGY_SHIFT
#define EDGI_SPEC_ENERGY_SHIFT 13u
#endif

/*
 * 现场联调：外放/ESC-50 与训练分布差时 type 易一直为 0。
 * 1=提高 mic 增益、放宽稳定化、打印 [AI] score；验收稳定后改 0。
 */
#ifndef EDGI_AI_FIELD_TUNING
#define EDGI_AI_FIELD_TUNING 0
#endif

/* 1=开源 micro_speech 风格 CNN+log-mel；0=旧 MLP/模板 */
#ifndef EDGI_AI_USE_OPEN_CNN
#define EDGI_AI_USE_OPEN_CNN 1
#endif

#ifndef EDGI_AI_USE_DEMO_CENTROID
#define EDGI_AI_USE_DEMO_CENTROID 0
#endif

#ifndef EDGI_AUDIO_MIC_VOLUME
#define EDGI_AUDIO_MIC_VOLUME 52
#endif

rt_err_t edgi_audio_capture_init(void);
rt_ssize_t edgi_audio_capture_read_frame(int16_t *buf, rt_size_t samples);

/**
 * 暂停/继续采集：暂停时 read_frame 只返回静音帧，不读 mic0（可与按键联动）。
 * 可在 GPIO 中断里调用，内部仅写标志位。
 */
void edgi_audio_capture_set_paused(rt_bool_t paused);
rt_bool_t edgi_audio_capture_is_paused(void);

rt_err_t edgi_audio_playback_start(void);
rt_ssize_t edgi_audio_playback_write(const void *data, rt_size_t bytes);
void edgi_audio_playback_stop(void);

#endif /* EDGI_AUDIO_CAPTURE_H */
