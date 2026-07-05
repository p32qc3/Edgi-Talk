#ifndef APP_AI_H
#define APP_AI_H

#include <rtthread.h>
#include <stdint.h>
#include "protocol.h"

/**
 * 轻量 MLP 推理（默认）或后续可换 TFLM+.tflite；与 app_ai.c 中 EDGI_USE_MODEL_STUB 配合。
 */
int app_ai_open_cnn_init(void);
int app_ai_open_cnn_push_pcm(const int16_t *pcm, unsigned nsamples);
int app_ai_open_cnn_infer(float prob_out[ALARM_TYPE_MAX]);

int app_ai_tflm_init(void);
void app_ai_tflm_deinit(void);
int app_ai_tflm_infer(const uint8_t spec[40][40], float prob_out[ALARM_TYPE_MAX]);

int app_ai_demo_centroid_init(void);
int app_ai_demo_centroid_infer(const uint8_t spec[40][40], float prob_out[ALARM_TYPE_MAX]);
void edgi_demo_cal_store(uint8_t class_1_to_4);
void edgi_demo_cal_stat_print(void);

/**
 * 启动 AI 推理线程：从 PDM 取帧 -> 特征 -> Ethos-U / TFLM -> 投递 AlarmResult
 * mq_alarm: 已创建的消息队列（elem 大小 >= sizeof(AlarmResult)）
 * 选题里写的“邮箱”若指 IPC，结构体请用消息队列；纯 mailbox 只能传 32 位值。
 */
rt_err_t app_ai_start(struct rt_messagequeue *mq_alarm);

#endif /* APP_AI_H */
