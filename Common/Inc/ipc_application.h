#ifndef IPC_APPLICATION_H
#define IPC_APPLICATION_H

#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

//来源有效位：分别标识Camera、USB和Control记录
#define IPC_APP_CAMERA (1UL << 0)
#define IPC_APP_USB (1UL << 1)
#define IPC_APP_CONTROL (1UL << 2)
#define IPC_APP_ALL (IPC_APP_CAMERA | IPC_APP_USB | IPC_APP_CONTROL)

//跨核固定格式编号，不依赖Camera枚举的实际数值
#define IPC_IMAGE_UNKNOWN 0U
#define IPC_IMAGE_Y8 1U
#define IPC_IMAGE_RGB565 2U
#define IPC_IMAGE_YUY2 3U
#define IPC_IMAGE_JPEG 4U

//业务载荷固定长度，两核必须使用同一布局
#define IPC_APPLICATION_BYTES 80U

//来源年龄和接收等待时间均需满足的新鲜度期限
#define IPC_APPLICATION_FRESH_MS 500U

//V3正式业务载荷，全部字段为固定32位整数
typedef struct
{
    uint32_t valid_mask;//有效来源位图
    uint32_t camera_state;//Camera任务状态：0停止、1运行、2停止中
    uint32_t usb_state;//USB任务状态：0停止、1已启动、2停止中
    uint32_t control_state;//Control协调状态，0到6
    uint32_t camera_fps_milli;//采集最近统计窗口的实测FPS乘1000
    uint32_t usb_fps_milli;//USB最近统计窗口的完成FPS乘1000
    uint32_t captured_frames;//实际完成采集帧数，允许无符号回绕
    uint32_t dropped_frames;//采集丢帧数，允许无符号回绕
    uint32_t usb_completed_frames;//USB实际完成帧数，不是提交次数
    uint32_t width;//已成功应用的图像宽度，未知为0
    uint32_t height;//已成功应用的图像高度，未知为0
    uint32_t format;//正式IPC图像格式编号，不直接传枚举对象
    int32_t last_fault;//Control最近处理的实际故障，包括自身超时
    int32_t camera_result;//Camera最近操作结果
    int32_t usb_result;//USB最近操作结果
    int32_t control_result;//Control最近操作结果
    uint32_t camera_age_ms;//发布时Camera本地记录年龄
    uint32_t usb_age_ms;//发布时USB本地记录年龄
    uint32_t control_age_ms;//发布时Control本地记录年龄
    uint32_t snapshot_id;//汇总采样编号，允许回绕，不是会话标识
} IpcApplicationStatus_t;

//CM4返回的完整本地副本，元数据不进入共享协议
typedef struct
{
    IpcApplicationStatus_t application;//完整业务副本，调用者不能修改共享区
    uint32_t valid_mask;//有结构合法的来源记录，不代表新鲜
    uint32_t fresh_mask;//来源年龄与通信状态均满足要求
    uint32_t receive_age_ms;//当前距离最后新业务编号的本地时间
    uint32_t peer_state;//本地Heartbeat观察状态
    SysResult_t read_result;//最近一次协议读取结果
} IpcApplicationView_t;

//本地缓存只由运行期唯一执行者更新
typedef struct
{
    IpcApplicationStatus_t application;
    uint32_t received_tick;//最后收到新采样编号的本核时刻
    bool has_sample;//是否已经接收过有效业务样本
    bool expired;//同一业务编号超时后保持陈旧，直到新编号到来
} IpcApplicationCache_t;

//校验正式业务字段，不依赖Camera或FreeRTOS类型
bool IPC_Application_Validate(const IpcApplicationStatus_t *application);

//检查CM4载荷是否保持全零
bool IPC_Application_IsEmpty(const IpcApplicationStatus_t *application);

//接收新的业务采样编号，重复采样不能刷新数据年龄
void IPC_Application_Observe(IpcApplicationCache_t *cache,
    const IpcApplicationStatus_t *application, uint32_t now);

//生成带有效性和新鲜度的本地视图
SysResult_t IPC_Application_GetView(const IpcApplicationCache_t *cache,
    uint32_t now, uint32_t peer_state, SysResult_t read_result,
    IpcApplicationView_t *view);

//编译期锁定80B载荷和字段偏移，布局变化时立即报错
#define IPC_APP_ASSERT(name, value) typedef char name[(value) ? 1 : -1]
IPC_APP_ASSERT(Ipc_AppSize, sizeof(IpcApplicationStatus_t) == IPC_APPLICATION_BYTES);
IPC_APP_ASSERT(Ipc_App_valid_mask, offsetof(IpcApplicationStatus_t, valid_mask) == 0U);
IPC_APP_ASSERT(Ipc_App_camera_state, offsetof(IpcApplicationStatus_t, camera_state) == 4U);
IPC_APP_ASSERT(Ipc_App_usb_state, offsetof(IpcApplicationStatus_t, usb_state) == 8U);
IPC_APP_ASSERT(Ipc_App_control_state, offsetof(IpcApplicationStatus_t, control_state) == 12U);
IPC_APP_ASSERT(Ipc_App_camera_fps_milli, offsetof(IpcApplicationStatus_t, camera_fps_milli) == 16U);
IPC_APP_ASSERT(Ipc_App_usb_fps_milli, offsetof(IpcApplicationStatus_t, usb_fps_milli) == 20U);
IPC_APP_ASSERT(Ipc_App_captured_frames, offsetof(IpcApplicationStatus_t, captured_frames) == 24U);
IPC_APP_ASSERT(Ipc_App_dropped_frames, offsetof(IpcApplicationStatus_t, dropped_frames) == 28U);
IPC_APP_ASSERT(Ipc_App_usb_completed_frames, offsetof(IpcApplicationStatus_t, usb_completed_frames) == 32U);
IPC_APP_ASSERT(Ipc_App_width, offsetof(IpcApplicationStatus_t, width) == 36U);
IPC_APP_ASSERT(Ipc_App_height, offsetof(IpcApplicationStatus_t, height) == 40U);
IPC_APP_ASSERT(Ipc_App_format, offsetof(IpcApplicationStatus_t, format) == 44U);
IPC_APP_ASSERT(Ipc_App_last_fault, offsetof(IpcApplicationStatus_t, last_fault) == 48U);
IPC_APP_ASSERT(Ipc_App_camera_result, offsetof(IpcApplicationStatus_t, camera_result) == 52U);
IPC_APP_ASSERT(Ipc_App_usb_result, offsetof(IpcApplicationStatus_t, usb_result) == 56U);
IPC_APP_ASSERT(Ipc_App_control_result, offsetof(IpcApplicationStatus_t, control_result) == 60U);
IPC_APP_ASSERT(Ipc_App_camera_age_ms, offsetof(IpcApplicationStatus_t, camera_age_ms) == 64U);
IPC_APP_ASSERT(Ipc_App_usb_age_ms, offsetof(IpcApplicationStatus_t, usb_age_ms) == 68U);
IPC_APP_ASSERT(Ipc_App_control_age_ms, offsetof(IpcApplicationStatus_t, control_age_ms) == 72U);
IPC_APP_ASSERT(Ipc_App_snapshot_id, offsetof(IpcApplicationStatus_t, snapshot_id) == 76U);
#undef IPC_APP_ASSERT

#endif
