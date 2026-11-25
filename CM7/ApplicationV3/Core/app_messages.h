#ifndef CM7_APP_MESSAGES_H
#define CM7_APP_MESSAGES_H

#include "runtime_event.h"
#include "camera_config.h"
#include <stdbool.h>

//固定业务通道编号，保留既有消息路由约定
typedef enum
{
    APP_CHANNEL_CAMERA_COMMAND = 0,
    APP_CHANNEL_USB_COMMAND,
    APP_CHANNEL_CONTROL,
    APP_CHANNEL_TO_CAMERA,
    APP_CHANNEL_TO_USB,
    APP_CHANNEL_STARTUP,
    APP_CHANNEL_COUNT,

    //录像使用旧禁用通道对应的静态槽，旧通道编号仍保持拒绝语义。
    APP_CHANNEL_STORAGE_TO_CAMERA = APP_CHANNEL_COUNT,
    APP_CHANNEL_CAMERA_TO_STORAGE,
    //独立网络帧通道；已有通道编号保持不变。
    APP_CHANNEL_NETWORK_TO_CAMERA,
    APP_CHANNEL_CAMERA_TO_NETWORK
} AppChannel_t;

//各消息类型用于识别命令、结果和帧交接协议
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
    APP_MESSAGE_FRAME_CANCEL_REQUEST,

    //USB实际消费终结回复后确认。同一上行FIFO保证确认先于下一请求
    APP_MESSAGE_RETURN_ACK_CONSUMED
} AppMessageType_t;

//消息只复制描述与凭据，不复制SDRAM图像
typedef struct
{
    //标明消息承载申请、帧描述、结果或归还确认
    AppMessageType_t type;
    //发送任务身份，用于约束消息来源
    RuntimeEventTarget_t source;
    //发起者分配的命令编号，用于识别匹配结果
    uint32_t command_id;
    //采集会话号，隔离重启前的帧凭据
    uint32_t stream_epoch;
    //当前会话内唯一的帧租约编号
    uint32_t lease_id;
    //最近一次操作或终结结果，保留原错误码
    SysResult_t result;
    //Dispatcher分配的帧序号；物理槽索引只保存在Camera的Media租约中
    uint32_t frame_id;
    //事件或图像时间戳，单位毫秒
    uint32_t timestamp_ms;
    //只读图像描述，数据仍由实际持有者负责
    CameraFrame_t frame;
} AppMessage_t;

//准备静态消息队列
SysResult_t App_MessagesInit(void);

//非阻塞提交消息；未接入通道或数据通道上的非法类型明确拒绝
SysResult_t App_MessagesSend(AppChannel_t channel, const AppMessage_t *message);

//取得当前任务所属通道的下一条消息
bool App_MessagesReceive(AppChannel_t channel, AppMessage_t *message);

//检查当前任务是否为指定业务所有者
bool App_MessagesIsOwner(RuntimeEventTarget_t target);


#endif
