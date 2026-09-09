#ifndef PET_SERVICE_STATUS_H
#define PET_SERVICE_STATUS_H

#include "../common/pet_types.h"

#define PET_SERVICE_STATUS_TEXT_MAX 48

/* AI状态枚举 */
enum {
    PET_AI_DEMO = 0,      /* 演示模式（离线） */
    PET_AI_READY = 1,     /* AI就绪 */
    PET_AI_BUSY = 2,      /* AI处理中 */
    PET_AI_ERROR = 3      /* AI错误 */
};

/* 51连接状态枚举 */
enum {
    PET_51_WAIT = 0,      /* 等待连接 */
    PET_51_LINK = 1,      /* 已连接 */
    PET_51_ERROR = 2      /* 连接错误 */
};

/* 运行时状态（内部使用） */
enum {
    PET_AI_RUNTIME_OFF=0,
    PET_AI_RUNTIME_READY=1,
    PET_AI_RUNTIME_WAITING=2,
    PET_AI_RUNTIME_COMPLETED=3,
    PET_AI_RUNTIME_ERROR=4
};

/* 初始化服务状态模块 */
void pet_service_status_init(void);

/* 设置版本信息（如"AB1"） */
void pet_service_status_set_version(const char *version);

/* 设置AI状态 */
void pet_service_status_set_ai_state(u8 state);

/* 设置51连接状态 */
void pet_service_status_set_51_state(u8 state);

/* 格式化状态字符串（用于显示） */
int pet_service_status_format(char *out, u16 capacity,
    u8 ai_status, u8 link_ready);

/* Formats the latest states previously set by the communication and AI tasks. */
int pet_service_status_format_current(char *out, u16 capacity);

/* 计算AI运行时状态 */
u8 pet_service_ai_runtime(u8 service_running, u8 busy,
    u8 has_event, u8 event_type);

#endif
