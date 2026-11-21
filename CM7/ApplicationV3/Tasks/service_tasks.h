#ifndef CM7_V3_SERVICE_TASKS_H
#define CM7_V3_SERVICE_TASKS_H

#include "sys_result.h"

//创建 V3 业务任务，并登记各自的 BOOT 事件接收者。
SysResult_t CameraTask_Create(void);
SysResult_t UsbTask_Create(void);
SysResult_t StorageTask_Create(void);
SysResult_t ControlTask_Create(void);

#endif
