#ifndef CM7_APP_MESSAGES_H
#define CM7_APP_MESSAGES_H

#include "runtime_event.h"
#include "camera_config.h"
#include <stdbool.h>

typedef enum
{
    APP_CHANNEL_CAMERA_COMMAND = 0,
    APP_CHANNEL_USB_COMMAND,
    APP_CHANNEL_CONTROL,
    APP_CHANNEL_TO_CAMERA,
    APP_CHANNEL_TO_USB,
    APP_CHANNEL_COUNT
} AppChannel_t;

typedef enum
{
    APP_MESSAGE_START = 1,
    APP_MESSAGE_STOP,
    APP_MESSAGE_ACK,
    APP_MESSAGE_FAULT,
    APP_MESSAGE_FRAME_REQUEST,
    APP_MESSAGE_FRAME_OFFER,
    APP_MESSAGE_FRAME_RETURN,
    APP_MESSAGE_RETURN_ACK,

    //取消尚未取得帧的请求，不取消已有租借
    APP_MESSAGE_FRAME_CANCEL_REQUEST
} AppMessageType_t;

//消息只复制描述与凭据，不复制SDRAM图像
typedef struct
{
    AppMessageType_t type;
    RuntimeEventTarget_t source;
    uint32_t command_id;
    uint32_t stream_epoch;
    uint32_t lease_id;
    SysResult_t result;
    //完整帧的分发元数据，不参与原租约三元组的匹配
    uint32_t frame_id;
    uint32_t timestamp_ms;
    CameraFrame_t frame;
} AppMessage_t;

//准备静态消息队列
SysResult_t App_MessagesInit(void);

//非阻塞提交消息，队列满时由发送方保留状态并重试
SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message);

//取得当前任务所属通道的下一条消息
bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message);

//检查当前任务是否为指定业务所有者
bool App_MessagesIsOwner(RuntimeEventTarget_t target);

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
