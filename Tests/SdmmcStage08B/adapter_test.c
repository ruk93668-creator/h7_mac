//使用真实Platform和BSP，RTOS调度及队列接口由本测试受控注入
#include "idma_test.c"
#include "../../CM7/Platform/sd_block.c"
#include "sd_block_probe.c"

//保存应用BOOT状态，测试不修改正式任务启动流程
volatile AppTasksDebug_t g_app_tasks_debug;

//控制静态队列创建、等待、唤醒和调度器状态
static BaseType_t s_scheduler = taskSCHEDULER_RUNNING;
static uint32_t s_queue_count, s_queue_create_fail, s_queue_waits, s_queue_gives;

//仅在Mock中统计测试任务创建，不影响正式任务
static uint32_t s_sd_test_task_creates, s_sd_test_task_fail;

//比较测试数据，不链接宿主运行库
int memcmp(const void *a,const void *b,size_t n)
{
    const uint8_t *x=a,*y=b;
    while(n--){if(*x!=*y)return (int)*x-(int)*y;x++;y++;}return 0;
}

//模拟静态二值信号量创建
QueueHandle_t xQueueGenericCreateStatic(UBaseType_t length,UBaseType_t item,uint8_t *storage,
    StaticQueue_t *buffer,uint8_t type)
{
    (void)storage;(void)type;
    if(length!=1U || item!=0U || s_queue_create_fail)return NULL;
    return (QueueHandle_t)buffer;
}

#if defined(SD_STRESS_TEST_HOOKS)

//只在压力模拟测试中注入停止或数据扰动，不参与正式固件
void SdStress_TestWaitHook(void);

//只在压力模拟测试中推进受控时间和轮间操作
void SdStress_TestDelayHook(TickType_t ticks);
#endif

//模拟阻塞时发生的硬件事件，不把take成功等同数据成功
BaseType_t xQueueSemaphoreTake(QueueHandle_t queue,TickType_t ticks)
{
    (void)queue;s_queue_waits++;
    if(s_queue_count){s_queue_count=0U;return pdTRUE;}
    (void)Test_Wait(NULL,(uint32_t)ticks);
#if defined(SD_STRESS_TEST_HOOKS)
    SdStress_TestWaitHook();
#endif
    if(s_queue_count){s_queue_count=0U;return pdTRUE;}return pdFALSE;
}

//模拟中断唤醒，允许事件合并
BaseType_t xQueueGiveFromISR(QueueHandle_t queue,BaseType_t *wake)
{
    (void)queue;*wake=pdFALSE;s_queue_count=1U;s_queue_gives++;return pdTRUE;
}

//模拟内核状态，不启动真实Scheduler
BaseType_t xTaskGetSchedulerState(void){return s_scheduler;}

//测试任务创建桩只登记静态内存，不运行无限循环
TaskHandle_t xTaskCreateStatic(TaskFunction_t task,const char *const name,const uint32_t depth,
    void *const arg,UBaseType_t priority,StackType_t *const stack,StaticTask_t *const tcb)
{
    (void)task;(void)name;(void)depth;(void)arg;(void)priority;(void)stack;
    s_sd_test_task_creates++;
    return s_sd_test_task_fail ? NULL : (TaskHandle_t)tcb;
}
void vTaskDelay(TickType_t ticks)
{
    s_tick+=ticks;
#if defined(SD_STRESS_TEST_HOOKS)
    SdStress_TestDelayHook(ticks);
#endif
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task){(void)task;return 256U;}

//执行等待适配和写测试授权场景
uint32_t SdBlockAdapter_TestRun(uint32_t id)
{
    uint32_t i, before;
    BspSdmmcWaitOps_t other;
    Setup();s_case=40U;
    CHECK(BSP_SDMMC_MemoryInit()==SYS_OK);
    if(id==0U)
    {
        CHECK(SD_Block_Init()==SYS_ERR_INVALID_STATE);CHECK(s_calls==0U);return 0U;
    }
    if(id==1U)s_queue_create_fail=1U;
    CHECK(SdBlock_Prepare()==SYS_OK);
    if(id==1U)
    {
        CHECK(s_prepare_result==SYS_ERR_NO_RESOURCE);CHECK(SD_Block_Init()==SYS_ERR_INVALID_STATE);return 0U;
    }
    CHECK(s_prepare_result==SYS_OK);
    if(id==2U){s_scheduler=taskSCHEDULER_NOT_STARTED;CHECK(SD_Block_Init()==SYS_ERR_INVALID_STATE);return 0U;}
    if(id==3U){__disable_irq();CHECK(!SdBlock_CanWait(NULL));return 0U;}
    if(id==4U){__set_BASEPRI(80U);CHECK(!SdBlock_CanWait(NULL));return 0U;}
    CHECK(SD_Block_Init()==SYS_OK);
    s_sd.SdCard.CardType=CARD_SDHC_SDXC;s_sd.SdCard.LogBlockNbr=s_card.logical_block_count;
    s_sd.CID[0]=1U;s_sd.CID[1]=2U;s_sd.CID[2]=3U;s_sd.CID[3]=4U;
    if(id==5U)
    {
        CHECK(SdBlock_Wait(NULL,0U)==SYS_ERR_INVALID_STATE);
        SdBlock_Notify(NULL);CHECK(SdBlock_Wait(NULL,1U)==SYS_OK);CHECK(s_queue_gives==1U);return 0U;
    }
    if(id==6U)
    {
        CHECK(SdBlock_Prepare()==SYS_OK);CHECK(s_prepare_result==SYS_OK);
        CHECK(SD_Block_Init()==SYS_OK);CHECK(s_calls==1U);return 0U;
    }
    if(id==7U)
    {
        other.can_wait=Test_CanWait;other.wait=Test_Wait;other.notify=Test_Notify;other.context=NULL;
        CHECK(BSP_SDMMC_SetWaitOps(&other)==SYS_ERR_ALREADY_EXISTS);return 0U;
    }
    if(id==8U){CHECK(SdBlockTest_Read()==SYS_OK);CHECK(g_sd_block_test.read_crc==g_sd_block_test.repeat_crc);CHECK(s_queue_waits!=0U);return 0U;}
    if(id==9U){CHECK(SD_Block_Read(16U,1U,s_test_data,100U)==SYS_OK);CHECK(s_queue_gives!=0U);return 0U;}
#if APP_SD_BLOCK_WRITE_TEST_ENABLED
    if(id==10U){s_sd.CID[0]=9U;CHECK(SdBlockTest_Write()==SYS_ERR_INVALID_ARG);CHECK(s_starts==0U);return 0U;}
    if(id==11U){s_card.capacity_bytes--;CHECK(SdBlockTest_Write()==SYS_ERR_INVALID_ARG);CHECK(s_starts==0U);return 0U;}
    if(id==12U){CHECK(SdBlockTest_Restore()==SYS_ERR_INVALID_STATE);CHECK(s_starts==0U);return 0U;}
    if(id==13U)
    {
        for(i=0U;i<sizeof(s_test_medium);i++)s_test_medium[i]=(uint8_t)(i^0x3CU);
        CHECK(SdBlockTest_Write()==SYS_OK);CHECK(g_sd_block_test.restore_required==0U);
        CHECK(g_sd_block_test.restore_result==SYS_OK);
        for(i=0U;i<sizeof(s_test_medium);i++)CHECK(s_test_medium[i]==(uint8_t)(i^0x3CU));return 0U;
    }
    if(id==14U)
    {
        g_sd_block_test.restore_required=1U;s_sd.CID[1]=99U;before=s_starts;
        CHECK(SdBlockTest_Restore()==SYS_ERR_INVALID_ARG);CHECK(s_starts==before);
        CHECK(g_sd_block_test.restore_required==1U);return 0U;
    }
    if(id==15U)
    {
        s_card.logical_block_count=104U;CHECK(SdBlockTest_Authorized()==SYS_ERR_INVALID_ARG);CHECK(s_starts==0U);return 0U;
    }
#else
    (void)i;(void)before;
#endif
    return 0U;
}
