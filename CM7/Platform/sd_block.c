//块设备适配只负责RTOS等待，HAL和IDMA细节保持在BSP
#include <stddef.h>
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

//保存唯一访问任务，不向FatFS或BSP暴露FreeRTOS类型
static TaskHandle_t s_owner;

//记录当前访问模式，挂载期间不能借给原始块访问者
static SdBlockClient_t s_client;

//核对已取得访问权的任务环境
static bool SdBlock_HasOwner(void);

//检查调用环境允许阻塞和SDMMC中断唤醒
static bool SdBlock_CanWait(void *context);

//有限阻塞等待中断事件，超时后BSP仍会检查硬件状态
static SysResult_t SdBlock_Wait(void *context, uint32_t timeout_ms);

//从SDMMC中断唤醒当前唯一等待者
static void SdBlock_Notify(void *context);

//准备静态同步资源，不识卡也不读写扇区
static SysResult_t SdBlock_Prepare(void);

//当前任务取得唯一块设备访问权
SysResult_t SD_Block_Claim(SdBlockClient_t client)
{
    uint32_t mask;
    TaskHandle_t task;

    //不允许无效客户端或不可阻塞环境取得设备
    if (client != SD_BLOCK_CLIENT_FATFS && client != SD_BLOCK_CLIENT_RAW)
        return SYS_ERR_INVALID_ARG;
    if (s_prepare_result != SYS_OK || !SdBlock_CanWait(NULL))
        return SYS_ERR_INVALID_STATE;
    task = xTaskGetCurrentTaskHandle();
    if (task == NULL)
        return SYS_ERR_INVALID_STATE;

    //短临界区只提交所有者，不在其中操作SD卡
    mask = __get_PRIMASK();
    __disable_irq();
    if (s_owner != NULL)
    {
        SysResult_t result = s_owner == task && s_client == client ?
            SYS_ERR_ALREADY_EXISTS : SYS_ERR_BUSY;
        __set_PRIMASK(mask);
        return result;
    }
    s_owner = task;
    s_client = client;
    __set_PRIMASK(mask);
    return SYS_OK;
}

//当前任务归还安全空闲的设备
SysResult_t SD_Block_Release(SdBlockClient_t client)
{
    BspSdmmcIoDiagnostics_t io;
    SysResult_t result;
    uint32_t mask;

    //其他任务或另一种客户端不能释放当前租约
    if (!SD_Block_IsOwner(client))
        return SYS_ERR_INVALID_STATE;
    result = BSP_SDMMC_GetIoDiagnostics(&io);
    if (result != SYS_OK)
        return result;

    //硬件仍占用或恢复失败时保留所有权
    if (io.active != 0U || io.fault != 0U)
        return SYS_ERR_BUSY;
    mask = __get_PRIMASK();
    __disable_irq();
    s_owner = NULL;
    s_client = SD_BLOCK_CLIENT_NONE;
    __set_PRIMASK(mask);
    return SYS_OK;
}

//核对当前任务的访问租约
bool SD_Block_IsOwner(SdBlockClient_t client)
{
    return client != SD_BLOCK_CLIENT_NONE && s_client == client && SdBlock_HasOwner();
}

//读取当前卡状态副本
SysResult_t SD_Block_GetStatus(SdBlockStatus_t *status)
{
    SdBlockStatus_t value = {0};
    BspSdmmcIoDiagnostics_t io;
    BspSdmmcCardInfo_t info;
    uint32_t raw;
    SysResult_t result;

    //只有所有者可以读取与当前会话有关的卡状态
    if (status == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!SdBlock_HasOwner())
        return SYS_ERR_INVALID_STATE;
    result = BSP_SDMMC_ReadDetect(&raw, &value.present);
    if (result != SYS_OK)
        return result;
    result = BSP_SDMMC_GetIoDiagnostics(&io);
    if (result != SYS_OK)
        return result;

    //ready还需要当前插卡、传输模式和非故障状态共同成立
    value.busy = io.active != 0U;
    value.fault = io.fault != 0U;
    value.initialized = value.present && io.transfer_ready != 0U && !value.fault &&
        BSP_SDMMC_GetCardInfo(&info) == SYS_OK;
    *status = value;
    return SYS_OK;
}

//读取当前卡的身份副本
SysResult_t SD_Block_GetIdentity(uint32_t cid[4])
{
    if (!SdBlock_HasOwner())
        return SYS_ERR_INVALID_STATE;
    return BSP_SDMMC_GetIdentity(cid);
}

//确认同步写契约已经完成
SysResult_t SD_Block_Sync(void)
{
    SdBlockStatus_t status;
    SysResult_t result = SD_Block_GetStatus(&status);

    //不能把掉卡、在途或故障状态当作同步成功
    if (result != SYS_OK)
        return result;
    if (!status.present)
        return SYS_ERR_NOT_FOUND;
    if (status.fault)
        return SYS_ERR_IO;
    if (!status.initialized)
        return SYS_ERR_INVALID_STATE;
    if (status.busy)
        return SYS_ERR_BUSY;
    return SYS_OK;
}

//识卡并配置4-bit正式传输模式
SysResult_t SD_Block_Init(void)
{
    SysResult_t result;

    //只允许准备完成后的任务调用，不能在Initcall或ISR中开始块访问
    if (s_prepare_result != SYS_OK || !SdBlock_HasOwner())
        return SYS_ERR_INVALID_STATE;
    result = BSP_SDMMC_Init();
    if (result != SYS_OK && result != SYS_ERR_ALREADY_EXISTS)
        return result;
    return BSP_SDMMC_EnableTransferMode();
}

//读取逻辑扇区
SysResult_t SD_Block_Read(uint32_t lba, uint32_t count, void *buffer, uint32_t timeout_ms)
{
    if (!SdBlock_HasOwner())
        return SYS_ERR_INVALID_STATE;
    return BSP_SDMMC_ReadBlocks(lba, count, buffer, timeout_ms);
}

//写入逻辑扇区
SysResult_t SD_Block_Write(uint32_t lba, uint32_t count, const void *buffer, uint32_t timeout_ms)
{
    if (!SdBlock_HasOwner())
        return SYS_ERR_INVALID_STATE;
    return BSP_SDMMC_WriteBlocks(lba, count, buffer, timeout_ms);
}

//读取本地卡信息副本
SysResult_t SD_Block_GetInfo(BspSdmmcCardInfo_t *info)
{
    if (!SdBlock_HasOwner())
        return SYS_ERR_INVALID_STATE;
    return BSP_SDMMC_GetCardInfo(info);
}

//停止故障事务并撤销旧卡信息
SysResult_t SD_Block_Recover(void)
{
    if (!SdBlock_HasOwner())
        return SYS_ERR_INVALID_STATE;
    return BSP_SDMMC_Recover();
}

//核对任务环境及私有所有者
static bool SdBlock_HasOwner(void)
{
    return SdBlock_CanWait(NULL) && s_owner != NULL &&
        s_owner == xTaskGetCurrentTaskHandle();
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
