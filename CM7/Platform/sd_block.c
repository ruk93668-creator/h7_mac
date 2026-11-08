//块设备适配只负责RTOS等待，HAL和IDMA细节保持在BSP
#include "sd_block.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "framework_init.h"
#include "stm32h7xx.h"

//二值信号量只合并唤醒事件，不承载完成结果或缓冲区所有权
static StaticSemaphore_t s_event_storage;

//保存静态信号量句柄，不占用内核堆
static SemaphoreHandle_t s_event;

//记录等待适配初始化结果，不让SD故障阻断其他Initcall
static SysResult_t s_prepare_result = SYS_ERR_INVALID_STATE;

//检查调用环境允许阻塞和SDMMC中断唤醒
static bool SdBlock_CanWait(void *context);

//有限阻塞等待中断事件，超时后BSP仍会检查硬件状态
static SysResult_t SdBlock_Wait(void *context, uint32_t timeout_ms);

//从SDMMC中断唤醒当前唯一等待者
static void SdBlock_Notify(void *context);

//准备静态同步资源，不识卡也不读写扇区
static SysResult_t SdBlock_Prepare(void);

//识卡并配置4-bit正式传输模式
SysResult_t SD_Block_Init(void)
{
    SysResult_t result;

    //只允许准备完成后的任务调用，不能在Initcall或ISR中开始块访问
    if (s_prepare_result != SYS_OK || !SdBlock_CanWait(NULL))
        return SYS_ERR_INVALID_STATE;
    result = BSP_SDMMC_Init();
    if (result != SYS_OK && result != SYS_ERR_ALREADY_EXISTS)
        return result;
    return BSP_SDMMC_EnableTransferMode();
}

//读取逻辑扇区
SysResult_t SD_Block_Read(uint32_t lba, uint32_t count, void *buffer, uint32_t timeout_ms)
{
    return BSP_SDMMC_ReadBlocks(lba, count, buffer, timeout_ms);
}

//写入逻辑扇区
SysResult_t SD_Block_Write(uint32_t lba, uint32_t count, const void *buffer, uint32_t timeout_ms)
{
    return BSP_SDMMC_WriteBlocks(lba, count, buffer, timeout_ms);
}

//读取本地卡信息副本
SysResult_t SD_Block_GetInfo(BspSdmmcCardInfo_t *info)
{
    return BSP_SDMMC_GetCardInfo(info);
}

//停止故障事务并撤销旧卡信息
SysResult_t SD_Block_Recover(void)
{
    return BSP_SDMMC_Recover();
}

//检查任务可以阻塞等待
static bool SdBlock_CanWait(void *context)
{
    (void)context;

    //屏蔽IRQ或暂停调度器时不允许进入阻塞操作
    return s_event != NULL && __get_IPSR() == 0U && __get_PRIMASK() == 0U &&
        __get_BASEPRI() == 0U && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}

//等待一次事件或有限超时
static SysResult_t SdBlock_Wait(void *context, uint32_t timeout_ms)
{
    TickType_t ticks;
    (void)context;

    //至少等待一个Tick，不把不足一个Tick转换为忙轮询
    if (!SdBlock_CanWait(NULL) || timeout_ms == 0U)
        return SYS_ERR_INVALID_STATE;
    ticks = pdMS_TO_TICKS(timeout_ms);
    if (ticks == 0U)
        ticks = 1U;
    return xSemaphoreTake(s_event, ticks) == pdTRUE ? SYS_OK : SYS_ERR_TIMEOUT;
}

//合并SDMMC中断唤醒
static void SdBlock_Notify(void *context)
{
    BaseType_t wake = pdFALSE;
    (void)context;

    //IRQ优先级7满足FreeRTOS阈值，事件满时仍由硬件状态保证结果不丢失
    if (s_event != NULL)
    {
        (void)xSemaphoreGiveFromISR(s_event, &wake);
        portYIELD_FROM_ISR(wake);
    }
}

//准备块设备等待适配
static SysResult_t SdBlock_Prepare(void)
{
    BspSdmmcWaitOps_t ops;

    //只创建一次静态资源，不重复初始化已经登记的队列控制块
    if (s_event == NULL)
        s_event = xSemaphoreCreateBinaryStatic(&s_event_storage);
    if (s_event == NULL)
    {
        s_prepare_result = SYS_ERR_NO_RESOURCE;
        return SYS_OK;
    }
    ops.can_wait = SdBlock_CanWait;
    ops.wait = SdBlock_Wait;
    ops.notify = SdBlock_Notify;
    ops.context = NULL;
    s_prepare_result = BSP_SDMMC_SetWaitOps(&ops);
    return SYS_OK;
}

FRAMEWORK_INIT_REGISTER(SdBlock_Prepare, INIT_LEVEL_PLATFORM);
