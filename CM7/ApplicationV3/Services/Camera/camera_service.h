#ifndef CM7_V3_CAMERA_SERVICE_H
#define CM7_V3_CAMERA_SERVICE_H

#include "app_context.h"
#include "sys_result.h"

typedef enum
{
    CAMERA_SERVICE_START = 16,
    CAMERA_SERVICE_STOP,
    CAMERA_SERVICE_HARDWARE_QUERY,
    CAMERA_SERVICE_RECOVER
} CameraServiceOperation_t;

//由其他登记任务提交模拟命令；SYS_OK只表示成功入队
SysResult_t CameraService_Request(uint32_t command_id, CameraServiceOperation_t operation, uint32_t argument);

//静态Registry使用的启动入口，只有所属任务可以准备或撤回资源
//由所属任务逐轮推进真实操作，返回建议等待毫秒数
uint32_t AppV3_CameraAdvance(void);
void AppV3_CameraRun(void *argument);
SysResult_t AppV3_CameraInit(const AppV3Context_t *context);
void AppV3_CameraDiscard(void);

#endif
