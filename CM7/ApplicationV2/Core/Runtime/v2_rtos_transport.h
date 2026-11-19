// P8.2 静态消息运输及任务所有权接口。

#ifndef V2_RTOS_TRANSPORT_H
#define V2_RTOS_TRANSPORT_H

#include "runtime_event.h"
#include <stdbool.h>
#include "../../Modules/Camera/camera_endpoint.h"
#include "../../Modules/USB/usb_endpoint.h"
#include "../../Modules/Storage/storage_endpoint.h"

#define V2_RTOS_DEPTH 2U

// 在主线程、调度器启动前创建所有静态队列。不可重置已初始化的队列。
SysResult_t V2Rtos_Init(void);
// 只有目标任务收到真实 BOOT 事件后才开放本任务角色。
bool V2Rtos_Enter(RuntimeEventTarget_t task, uint32_t events);
// 检查当前任务已收到 BOOT，且允许使用自己的运输端口。
bool V2Rtos_Owner(RuntimeEventTarget_t task);

extern const VCameraTransport V2Rtos_Camera;
extern const VUSBTransport V2Rtos_Usb;
extern const VStorageTransport V2Rtos_Storage;
#endif
