#ifndef CM7_V3_STORAGE_SERVICE_H
#define CM7_V3_STORAGE_SERVICE_H

#include "app_context.h"
#include "sys_result.h"

typedef enum
{
    STORAGE_SERVICE_MOCK_START = 1,
    STORAGE_SERVICE_MOCK_STOP,
    STORAGE_SERVICE_SET_PARAMETER,
    STORAGE_SERVICE_QUERY,
    STORAGE_SERVICE_MOCK_FAIL,
    STORAGE_SERVICE_OP_COUNT
} StorageServiceOperation_t;

//由其他登记任务提交模拟命令；SYS_OK只表示成功入队
SysResult_t StorageService_Request(uint32_t command_id, StorageServiceOperation_t operation, uint32_t argument);

//静态Registry使用的启动入口，只有所属任务可以准备或撤回资源
void AppV3_StorageRun(void *argument);
SysResult_t AppV3_StorageInit(const AppV3Context_t *context);
void AppV3_StorageDiscard(void);

#endif
