//仅独立验收镜像替换app_tasks编译单元，不修改正式任务源码
#define App_TasksCreate FatFsAcceptance_CreateNormalTasks
#include "../../CM7/Application/app_tasks.c"
#undef App_TasksCreate
#include "hardware_acceptance.c"
#include "platform_time.h"

//验收任务静态控制块，不使用内核堆
static StaticTask_t s_accept_tcb;

//验收任务独立栈，不借用Camera或UsbTask栈
static StackType_t s_accept_stack[2048] __attribute__((aligned(8)));

//等待原BOOT屏障后处理人工验收命令
static void Acceptance_Task(void *argument);

//创建原有任务及仅验收镜像存在的独立任务
SysResult_t App_TasksCreate(void)
{
    SysResult_t result = FatFsAcceptance_CreateNormalTasks();
    if (result != SYS_OK)
        return result;

    //原created_tasks只统计事件表中的四个目标，验收任务不参加该事件表
    if (xTaskCreateStatic(Acceptance_Task, "FsAccept", 2048U, NULL, 1U,
        s_accept_stack, &s_accept_tcb) == NULL)
        return SYS_ERR_NO_RESOURCE;
    return SYS_OK;
}

//由人工命令控制文件验收
static void Acceptance_Task(void *argument)
{
    uint32_t request, stack_tick;
    (void)argument;
    while (g_app_tasks_debug.boot_released == 0U)
        vTaskDelay(pdMS_TO_TICKS(20U));
    g_fatfs_acceptance.ready = 1U;
    g_fatfs_acceptance.file_index = 1U;
    stack_tick = Platform_TimeGetMs();
    for (;;)
    {
        //默认request为0，不自动识卡、创建文件或写入
        request = g_fatfs_acceptance.request;
        if (request != 0U)
        {
            g_fatfs_acceptance.request = 0U;
            FatFsAcceptance_Process(request);
        }

        //诊断扫描最多每秒一次，避免重复引入高频扫描问题
        if ((uint32_t)(Platform_TimeGetMs() - stack_tick) >= 1000U)
        {
            stack_tick = Platform_TimeGetMs();
            g_fatfs_acceptance.stack_free_words = uxTaskGetStackHighWaterMark(NULL);
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}
