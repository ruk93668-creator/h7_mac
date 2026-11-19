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

#endif
