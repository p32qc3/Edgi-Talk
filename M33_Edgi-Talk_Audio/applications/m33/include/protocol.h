/**
 * 团队统一协议：算法层 -> 应用层 的告警结果
 * 与选题说明中的 AlarmResult / alarm_msg 对齐
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

/* 与赛题标签顺序一致，训练与推理必须相同 */
enum alarm_type_e
{
    ALARM_TYPE_BACKGROUND = 0, /* 环境/无紧急 */
    ALARM_TYPE_FIRE       = 1, /* 火警警报 */
    ALARM_TYPE_KNOCK      = 2, /* 敲门 */
    ALARM_TYPE_BABY       = 3, /* 婴儿啼哭 */
    ALARM_TYPE_BOILING    = 4, /* 水开（若模型未训练可保留占位） */
    ALARM_TYPE_MAX
};

typedef struct
{
    uint8_t  alarm_type;   /* alarm_type_e */
    uint8_t  confidence;   /* 0-100 */
    uint16_t _reserved;    /* 填 0；占位使 sizeof 为 8，满足 rt_mq 消息对齐 */
    uint32_t timestamp;    /* rt_tick_get() 等 */
} AlarmResult;

/* 应用层若使用简化结构，可与 AlarmResult 内存布局兼容 */
typedef struct
{
    int type;
    int confidence;
} alarm_msg;

#endif /* PROTOCOL_H */
