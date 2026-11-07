#ifndef CM7_APP_STATUS_H
#define CM7_APP_STATUS_H

#include "ipc_application.h"
#include "camera_config.h"

//Camera任务提交自己的本地运行记录，不传递图像或指针
typedef struct
{
    uint32_t state;//所属任务的运行或协调状态
    uint32_t fps_milli;//最近统计窗口的实测FPS乘1000
    uint32_t captured_frames;//实际完成采集的累计帧数
    uint32_t dropped_frames;//采集丢帧累计计数
    uint32_t width;//成功应用的图像宽度
    uint32_t height;//成功应用的图像高度
    CameraPixelFormat_t format;//本核Camera格式，由适配层映射到IPC编号
    bool config_valid;//是否已有成功应用的实际图像配置
    SysResult_t result;//所属任务最近操作结果
} AppCameraStatus_t;

//USB任务提交实际完成计数，不能用提交次数替代
typedef struct
{
    uint32_t state;//所属任务的运行或协调状态
    uint32_t fps_milli;//最近统计窗口的实测FPS乘1000
    uint32_t completed_frames;//USB真正完成的帧数，不是提交次数
    SysResult_t result;//所属任务最近操作结果
} AppUsbStatus_t;

//Control任务提交协调状态和最近故障
typedef struct
{
    uint32_t state;//所属任务的运行或协调状态
    SysResult_t result;//所属任务最近操作结果
    SysResult_t last_fault;//Control最近处理的实际故障
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
