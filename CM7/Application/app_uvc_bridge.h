#ifndef CM7_APP_UVC_BRIDGE_H
#define CM7_APP_UVC_BRIDGE_H
#include "app_messages.h"

typedef struct
{
    uint32_t stream_epoch;
    uint32_t acquired_frames;
    uint32_t submitted_frames;
    uint32_t released_frames;

    //Camera侧拒绝与队列重试计数
    uint32_t rejected_messages;
    uint32_t queue_retries;

    //USB侧独立计数，避免两任务共同执行非原子的加一
    uint32_t usb_rejected_messages;
    uint32_t usb_queue_retries;
    SysResult_t camera_result;
    SysResult_t usb_result;
} AppBridgeDebug_t;

//提供租借协议调试计数，不作为释放依据
extern volatile AppBridgeDebug_t g_bridge_debug;

//保存桥接所需的静态图像配置
SysResult_t App_UvcBridgeConfigure(const CameraConfig_t *config);

//建立新的采集代次
SysResult_t App_UvcBridgeCameraBegin(void);

//由CameraTask推进请求、Offer和归还确认
void App_UvcBridgeCameraProcess(bool enabled);

//由UsbTask推进请求、借用和Return
void App_UvcBridgeUsbProcess(bool enabled);

//确认USB侧已完成归还握手
bool App_UvcBridgeUsbIsIdle(void);

//确认Camera侧不再持有租借帧
bool App_UvcBridgeCameraIsIdle(void);
#endif
