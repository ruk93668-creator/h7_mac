#ifndef CM7_V3_STORAGE_SERVICE_H
#define CM7_V3_STORAGE_SERVICE_H

#include "app_context.h"
#include "sys_result.h"

//Storage硬件及录像业务命令编号
typedef enum
{
    STORAGE_SERVICE_START = 16,
    STORAGE_SERVICE_STOP,
    STORAGE_SERVICE_HARDWARE_QUERY,
    STORAGE_SERVICE_RECOVER,
    STORAGE_SERVICE_RECORD_START, //argument为非零文件编号，已有文件保持原样
    STORAGE_SERVICE_RECORD_STOP,
    STORAGE_SERVICE_RECORD_QUERY
} StorageServiceOperation_t;

//录像查询primary/detail分别为成功写入帧数和可用存储扇区。
#define STORAGE_RECORD_ACTIVE  8U
//录像文件仍处于打开状态
#define STORAGE_RECORD_FILE   16U
//停止新申请后正在等待原资源退出
#define STORAGE_RECORD_DRAIN  32U

//由其他登记任务提交命令；SYS_OK只表示成功入队
SysResult_t StorageService_Request(uint32_t command_id, StorageServiceOperation_t operation, uint32_t argument);

//静态Registry使用的启动入口，只有所属任务可以准备或撤回资源
//由所属任务逐轮推进真实操作，返回建议等待毫秒数
uint32_t AppV3_StorageAdvance(void);
//从Storage任务上下文运行专属循环
void AppV3_StorageRun(void *argument);
//由Storage任务建立邮箱并检查录像配置
SysResult_t AppV3_StorageInit(const AppV3Context_t *context);
//撤回尚未发布的Storage资源
void AppV3_StorageDiscard(void);

#endif
