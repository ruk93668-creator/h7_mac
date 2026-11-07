#ifndef CM7_APP_STATUS_H
#define CM7_APP_STATUS_H

#include "ipc_application.h"
#include "camera_config.h"

//Camera任务提交自己的本地运行记录，不传递图像或指针
typedef struct
{
    uint32_t state;
    uint32_t fps_milli;
    uint32_t captured_frames;
    uint32_t dropped_frames;
    uint32_t width;
    uint32_t height;
    CameraPixelFormat_t format;
    bool config_valid;
    SysResult_t result;
} AppCameraStatus_t;

//USB任务提交实际完成计数，不能用提交次数替代
typedef struct
{
    uint32_t state;
    uint32_t fps_milli;
    uint32_t completed_frames;
    SysResult_t result;
} AppUsbStatus_t;

//Control任务提交协调状态和最近故障
typedef struct
{
    uint32_t state;
    SysResult_t result;
    SysResult_t last_fault;
} AppControlStatus_t;

//只允许Camera任务提交自己的完整记录
SysResult_t App_StatusPublishCamera(const AppCameraStatus_t *status);

//只允许Usb任务提交自己的完整记录
SysResult_t App_StatusPublishUsb(const AppUsbStatus_t *status);

//只允许Control任务提交自己的完整记录
SysResult_t App_StatusPublishControl(const AppControlStatus_t *status);

//由IPC任务取得三个来源的一致汇总，年龄采用本地Tick
SysResult_t App_StatusGetSnapshot(IpcApplicationStatus_t *snapshot);

//同时取得本核采样时刻，只供IPC发布方计算同核等待延迟
SysResult_t App_StatusGetSnapshotAt(IpcApplicationStatus_t *snapshot, uint32_t *sampled_tick);

//Watch查看最近一次汇总，不保证调试器同时刷新全部字段
extern volatile IpcApplicationStatus_t g_app_status_debug;

#endif
