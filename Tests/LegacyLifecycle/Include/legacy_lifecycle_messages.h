#ifndef TESTS_LEGACY_LIFECYCLE_MESSAGES_H
#define TESTS_LEGACY_LIFECYCLE_MESSAGES_H

//生命周期消息使用独立类型和队列，旧帧消息布局与通道编号保持不变
#include "app_camera_lifecycle.h"

typedef enum
{
    APP_LIFECYCLE_CHANNEL_CAMERA = 0,
    APP_LIFECYCLE_CHANNEL_USB,
    APP_LIFECYCLE_CHANNEL_STORAGE,
    APP_LIFECYCLE_CHANNEL_COUNT
} AppLifecycleChannel_t;

//只准备三条四项静态队列，不执行任何业务START或STOP
SysResult_t App_LifecycleMessagesInit(void);

//检查消息方向及真实任务身份，成功只表示完整消息已经入队
SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t channel, const AppLifecycleMessage_t *message);

//只有对应所有者任务可以取消息，通知位不作为交付依据
bool App_LifecycleMessagesReceive(AppLifecycleChannel_t channel, AppLifecycleMessage_t *message);

#endif
