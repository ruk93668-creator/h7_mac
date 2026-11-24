#ifndef CM7_V3_CAMERA_SERVICE_H
#define CM7_V3_CAMERA_SERVICE_H

#include "app_context.h"
#include "sys_result.h"

//Camera硬件命令编号，供登记任务提交
typedef enum
{
    CAMERA_SERVICE_START = 16,
    CAMERA_SERVICE_STOP,
    CAMERA_SERVICE_HARDWARE_QUERY,
    CAMERA_SERVICE_RECOVER
} CameraServiceOperation_t;

//登记任务可提交硬件命令，SYS_OK只表示成功入队
SysResult_t CameraService_Request(uint32_t command_id, CameraServiceOperation_t operation, uint32_t argument);

//静态Registry使用的启动入口，只有所属任务可以准备或撤回资源
//由所属任务逐轮推进真实操作，返回建议等待毫秒数
uint32_t AppV3_CameraAdvance(void);
//从Camera任务上下文运行服务循环直至调度终止
void AppV3_CameraRun(void *argument);
//由Camera任务初始化邮箱与采集配置
SysResult_t AppV3_CameraInit(const AppV3Context_t *context);
//撤回尚未发布的Camera资源
void AppV3_CameraDiscard(void);

#endif
