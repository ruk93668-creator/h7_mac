//SDMMC1只负责低速1-bit识卡与诊断，不接文件系统或视频链路
#include "bsp_sdmmc.h"
#include "bsp_sdram.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

//复用摄像头已经启用的PLL1Q，不修改PLL参数
#define SDMMC_KERNEL_HZ 240000000UL

//首版初始化及识卡完成后都保持不高于400kHz
#define SDMMC_TARGET_HZ 400000UL

//临时对照实验：每次只复制8KiB，不改变32KiB的IDMA批量
#define SDMMC_COPY_CHUNK_BYTES 8192U

//设为0可对照无间隔分段复制；当前先验证段间等待至少1ms
#ifndef SDMMC_COPY_GAP_MS
#define SDMMC_COPY_GAP_MS 1U
#endif

#if SDMMC_COPY_GAP_MS > 1U
#error SDMMC_COPY_GAP_MS_must_be_zero_or_one
#endif

//保存SDMMC1私有HAL句柄，不向Application公开
static SD_HandleTypeDef s_sd;

//保存完整卡信息，仅初始化全部成功后有效
static BspSdmmcCardInfo_t s_card;

//保存最近初始化及清理结果，错误清理前先锁存HAL信息
static BspSdmmcDiagnostics_t s_diagnostics;

//记录同一时刻是否已有调用者操作，不能让两个任务交错初始化
static bool s_busy;

//记录MSP已占用的SDMMC和GPIO资源，失败后不能虚假释放
static bool s_hw_ready;

//保存静态等待适配，具体RTOS同步机制由Platform提供
static BspSdmmcWaitOps_t s_wait_ops;

//记录块事务、原始错误和恢复结果
static BspSdmmcIoDiagnostics_t s_io;


//IRQ只锁存硬件状态，任务侧调用HAL处理终止事件
static volatile uint32_t s_irq_status;

//回调只确认当前方向的完成，错误始终优先于完成
static volatile bool s_batch_active;

//回调在任务侧推进，仅记录本批方向和完成状态
static bool s_batch_write, s_batch_done;
static uint32_t s_batch_error;

//本文件只把专用AXI缓冲区交给IDMA
extern uint8_t g_sdmmc_dma_buffer[BSP_SDMMC_DMA_BYTES];

//执行完整块请求并按32KiB分批
static SysResult_t Sdmmc_Transfer(uint32_t lba, uint32_t count, void *buffer,
    uint32_t timeout_ms, bool write);

//验证调用者CPU缓冲区及长度，排除专用DMA区域和地址回绕
static bool Sdmmc_CpuBufferValid(const void *buffer, uint32_t bytes, bool write);

//分段准备AXI写缓冲，保持原请求截止时间和源数据所有权
static SysResult_t Sdmmc_CopyToAxi(const uint8_t *source, uint32_t bytes,
    uint32_t started, uint32_t timeout_ms);

//按内存地址执行原有复制，不改变中断开关或段间等待
static void Sdmmc_CopyBytes(uint8_t *destination, const uint8_t *source, uint32_t bytes);

//处理终止事件并等待卡返回TRANSFER
static SysResult_t Sdmmc_WaitBatch(uint32_t started, uint32_t timeout_ms);

//确认IDMA和数据状态机已经停止访问缓冲区
static bool Sdmmc_Quiescent(void);

//停止失败事务，不能仅凭HAL返回值释放所有权
static SysResult_t Sdmmc_StopIo(void);

//捕获错误并按任务上下文调用HAL IRQ处理，避免中断内同步等待
static void Sdmmc_ServiceEvents(void);

//检查普通线程和本工程TIM6时间基准是否允许执行同步HAL
static bool Sdmmc_CanInit(void);

//短临界区取得本核操作权，长时间HAL调用仍保持中断开启
static bool Sdmmc_Take(void);

//释放本核操作权
static void Sdmmc_Give(void);

//把HAL状态与SD错误位转换为单次操作结果
static SysResult_t Sdmmc_Result(HAL_StatusTypeDef status, uint32_t error);

//只选择现有PLL1Q，检查共享SDMMC时钟及实际频率
static SysResult_t Sdmmc_Clock(void);

//核对实际输出时钟、总线宽度和未启用的IDMA状态
static bool Sdmmc_ConfigValid(void);

//保存原始失败信息，再释放已经申请的硬件资源
static SysResult_t Sdmmc_Finish(SysResult_t result, HAL_StatusTypeDef status, uint32_t started);

//只清理SDMMC1及其专用引脚，不关闭共用GPIO端口时钟
static SysResult_t Sdmmc_Cleanup(void);

//读取插卡检测输入
SysResult_t BSP_SDMMC_ReadDetect(uint32_t *raw_level, bool *present)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t raw;

    //输出参数为空时不能配置或写入调用者内存
    if (raw_level == NULL || present == NULL)
        return SYS_ERR_INVALID_ARG;

    //检测输入只供普通线程访问，不在ISR中重配GPIO
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //拆除R49上拉后由内部下拉保持无卡为低，插卡接通VDD变高
    __HAL_RCC_GPIOH_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOH, &gpio);
    raw = HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_14) == GPIO_PIN_SET ? 1U : 0U;

    //同时返回原始电平和解释结果，方便先核实有效极性
    *raw_level = raw;
    *present = BSP_SDMMC_DETECT_ACTIVE_LOW ? raw == 0U : raw != 0U;
    return SYS_OK;
}

//低速1-bit识别SD卡并提交完整卡信息
SysResult_t BSP_SDMMC_Init(void)
{
    HAL_SD_CardInfoTypeDef info;
    HAL_StatusTypeDef status;
    SysResult_t result;
    uint32_t raw = 0U, started, state;
    bool present = false;

    //HAL内部需要Tick和中断推进，禁止从ISR或Tick被屏蔽时调用
    if (!Sdmmc_CanInit())
        return SYS_ERR_INVALID_STATE;

    //只保护本次操作身份，不长时间屏蔽中断
    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;

    //已有有效信息时拒绝重复初始化，换卡必须先执行DeInit
    if (s_diagnostics.ready != 0U)
    {
        Sdmmc_Give();
        return SYS_ERR_ALREADY_EXISTS;
    }

    //上次清理失败时先显式重试DeInit，不能覆盖仍占用的句柄
    if (s_hw_ready || s_io.fault != 0U)
    {
        Sdmmc_Give();
        return SYS_ERR_INVALID_STATE;
    }

    //每次尝试先清掉旧卡数据，不能在新失败后交付上次容量
    started = HAL_GetTick();
    memset(&s_card, 0, sizeof(s_card));
    memset(&s_sd, 0, sizeof(s_sd));
    s_diagnostics.init_attempts++;
    s_diagnostics.stage = BSP_SDMMC_STAGE_DETECT;
    s_diagnostics.detect_active_low = BSP_SDMMC_DETECT_ACTIVE_LOW;
    s_diagnostics.kernel_clock_hz = 0U;
    s_diagnostics.sd_clock_hz = 0U;
    s_diagnostics.clock_div = 0U;
    s_diagnostics.bus_width = 0U;
    s_diagnostics.cleanup_result = SYS_OK;
    s_diagnostics.rcc_status = HAL_OK;
    s_diagnostics.status_register = 0U;
    s_diagnostics.clock_register = 0U;
    s_diagnostics.response1 = 0U;
    result = BSP_SDMMC_ReadDetect(&raw, &present);
    s_diagnostics.detect_raw = raw;
    s_diagnostics.card_present = present ? 1U : 0U;
    if (result != SYS_OK || !present)
        return Sdmmc_Finish(result != SYS_OK ? result : SYS_ERR_NOT_FOUND, HAL_OK, started);

    //配置时钟选择器之前验证PLL，不能为识卡重建系统PLL
    s_diagnostics.stage = BSP_SDMMC_STAGE_CLOCK;
    result = Sdmmc_Clock();
    if (result != SYS_OK)
        return Sdmmc_Finish(result, HAL_OK, started);

    //所有参数显式填写，HAL识卡后仍使用低速单数据线
    memset(&s_sd, 0, sizeof(s_sd));
    s_sd.Instance = SDMMC1;
    s_sd.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;//CLK上升沿采样
    s_sd.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;//识卡期间不自动停钟
    s_sd.Init.BusWide = SDMMC_BUS_WIDE_1B;//只使用DAT0，不切换4-bit
    s_sd.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;//首版不启用硬件流控
    s_sd.Init.ClockDiv = (s_diagnostics.kernel_clock_hz + 2U * SDMMC_TARGET_HZ - 1U) /
        (2U * SDMMC_TARGET_HZ);

    //HAL通过本文件MSP配置PC8、PC12和PD2，全程使用轮询而非DMA
    s_diagnostics.stage = BSP_SDMMC_STAGE_INIT;
    status = HAL_SD_Init(&s_sd);
    if (status != HAL_OK)
        return Sdmmc_Finish(Sdmmc_Result(status, HAL_SD_GetError(&s_sd)), status, started);

    //HAL可能重新配置CLKCR，必须检查实际结果而非只看Init参数
    if (!Sdmmc_ConfigValid())
        return Sdmmc_Finish(SYS_ERR_PROTOCOL, HAL_OK, started);

    //GetCardInfo只复制HAL已有信息，仍需验证容量和实时协议状态
    s_diagnostics.stage = BSP_SDMMC_STAGE_INFO;
    status = HAL_SD_GetCardInfo(&s_sd, &info);
    if (status != HAL_OK)
        return Sdmmc_Finish(Sdmmc_Result(status, HAL_SD_GetError(&s_sd)), status, started);

    if (info.LogBlockNbr == 0U || info.LogBlockSize != 512U ||
        (info.CardType != CARD_SDSC && info.CardType != CARD_SDHC_SDXC))
        return Sdmmc_Finish(SYS_ERR_PROTOCOL, HAL_OK, started);

    //CMD13确认卡实际处于TRANSFER，不把缓存信息当成识卡完成证明
    s_diagnostics.stage = BSP_SDMMC_STAGE_STATE;
    state = HAL_SD_GetCardState(&s_sd);
    if (HAL_SD_GetError(&s_sd) != HAL_SD_ERROR_NONE || state != HAL_SD_CARD_TRANSFER)
        return Sdmmc_Finish(HAL_SD_GetError(&s_sd) != HAL_SD_ERROR_NONE ?
            Sdmmc_Result(HAL_ERROR, HAL_SD_GetError(&s_sd)) : SYS_ERR_INVALID_STATE, HAL_ERROR, started);

    //一次性形成有效副本，容量先转64位再相乘
    s_card.card_type = info.CardType;
    s_card.card_version = info.CardVersion;
    s_card.logical_block_count = info.LogBlockNbr;
    s_card.logical_block_size = info.LogBlockSize;
    s_card.physical_block_count = info.BlockNbr;
    s_card.physical_block_size = info.BlockSize;
    s_card.capacity_bytes = (uint64_t)info.LogBlockNbr * info.LogBlockSize;
    s_card.card_state = state;
    s_diagnostics.ready = 1U;
    s_diagnostics.stage = BSP_SDMMC_STAGE_READY;
    return Sdmmc_Finish(SYS_OK, HAL_OK, started);
}

//取得已经验证通过的卡信息
SysResult_t BSP_SDMMC_GetCardInfo(BspSdmmcCardInfo_t *info)
{
    uint32_t raw;
    bool present;
    SysResult_t result;

    //读取接口只交付完整本地副本，不在ISR里重配检测输入
    if (info == NULL)
        return SYS_ERR_INVALID_ARG;

    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;

    //失败或尚未初始化时保留调用者输出不变
    result = s_diagnostics.ready != 0U ? SYS_OK : SYS_ERR_INVALID_STATE;
    if (result == SYS_OK)
    {
        result = BSP_SDMMC_ReadDetect(&raw, &present);
        if (result == SYS_OK && !present)
        {

            //观察到拔卡后撤销旧会话，重新插卡不能继续使用旧RCA和容量
            result = SYS_ERR_NOT_FOUND;
            s_diagnostics.ready = 0U;
            s_io.transfer_ready = 0U;
            s_io.fault = 1U;
        }

        if (result == SYS_OK)
            *info = s_card;
    }

    Sdmmc_Give();
    return result;
}

//取得完整诊断副本
SysResult_t BSP_SDMMC_GetDiagnostics(BspSdmmcDiagnostics_t *diagnostics)
{

    //初始化未结束时返回BUSY，不能交付半更新诊断
    if (diagnostics == NULL)
        return SYS_ERR_INVALID_ARG;

    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;

    *diagnostics = s_diagnostics;
    Sdmmc_Give();
    return SYS_OK;
}

//释放SDMMC1资源并使卡信息失效
SysResult_t BSP_SDMMC_DeInit(void)
{
    SysResult_t result;

    //DeInit也会调用HAL并访问外设，只允许正常可计时上下文
    if (!Sdmmc_CanInit())
        return SYS_ERR_INVALID_STATE;

    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;

    //故障仍持有DMA内存时必须先执行显式Recover，不能直接清理句柄
    if (s_io.active != 0U || s_io.fault != 0U)
    {
        Sdmmc_Give();
        return SYS_ERR_INVALID_STATE;
    }

    s_io.transfer_ready = 0U;
    s_io.high_speed = 0U;

    //先撤销信息有效性，清理失败也不能继续交付旧卡数据
    s_diagnostics.ready = 0U;
    memset(&s_card, 0, sizeof(s_card));
    result = Sdmmc_Cleanup();
    s_diagnostics.cleanup_result = result;
    Sdmmc_Give();
    return result;
}

//配置本私有句柄对应的SDMMC1引脚和时钟
void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef gpio = {0};

    //忽略其他句柄，避免无意操作未来其他SDMMC实例
    if (hsd != &s_sd || hsd->Instance != SDMMC1)
        return;

    //GPIO时钟可能与Camera或FMC共用，只开启、不在清理时关闭
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDMMC1_CLK_ENABLE();

    //任务阻塞后CPU可能WFI，传输期间保持SDMMC内核的Sleep时钟
    __HAL_RCC_SDMMC1_CLK_SLEEP_ENABLE();

    //只复位本外设，清除上次失败可能留下的DPSM和IDMA配置
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __HAL_RCC_SDMMC1_RELEASE_RESET();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDMMC1;

    //1-bit只配置DAT0与CLK，DAT1到DAT3保持未使用状态
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);
    s_hw_ready = true;
}

//释放本外设引脚，不干扰共用GPIO端口上的Camera和SDRAM
void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{

    //只处理当前BSP私有句柄，避免释放其他外设资源
    if (hsd != &s_sd || hsd->Instance != SDMMC1)
        return;

    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    __HAL_RCC_SDMMC1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
    s_hw_ready = false;
}

//绑定普通C等待适配
SysResult_t BSP_SDMMC_SetWaitOps(const BspSdmmcWaitOps_t *ops)
{
    SysResult_t result = SYS_OK;

    //适配必须完整，且只能在非中断上下文登记
    if (ops == NULL || ops->can_wait == NULL || ops->wait == NULL || ops->notify == NULL)
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;

    //不同适配不得替换仍可能被中断使用的静态对象
    if (s_wait_ops.wait != NULL && (s_wait_ops.wait != ops->wait ||
        s_wait_ops.can_wait != ops->can_wait || s_wait_ops.notify != ops->notify ||
        s_wait_ops.context != ops->context))
        result = SYS_ERR_ALREADY_EXISTS;
    else
        s_wait_ops = *ops;
    Sdmmc_Give();
    return result;
}

//低速识卡后配置正式4-bit读写模式
SysResult_t BSP_SDMMC_EnableTransferMode(void)
{
    GPIO_InitTypeDef gpio = {0};
    HAL_StatusTypeDef status;
    SysResult_t result;
    uint32_t state;

    //DMA区必须已配置，速度切换仅在任务可以阻塞的上下文执行
    if (!Sdmmc_CanInit() || !BSP_SDMMC_MemoryValid() || s_wait_ops.can_wait == NULL ||
        !s_wait_ops.can_wait(s_wait_ops.context))
        return SYS_ERR_INVALID_STATE;
    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;
    if (s_diagnostics.ready == 0U || s_io.fault != 0U)
    {
        Sdmmc_Give();
        return SYS_ERR_INVALID_STATE;
    }
    if (s_io.transfer_ready != 0U)
    {
        Sdmmc_Give();
        return SYS_OK;
    }

    //每次重新配置都从未知速度开始，不能继承上一张卡的高速标记
    s_io.high_speed = 0U;

    //补齐DAT1到DAT3，原有CMD和DAT0配置保持不变
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDMMC1;
    HAL_GPIO_Init(GPIOC, &gpio);

    //在400kHz完成总线宽度协商，拒绝HAL意外切换到超频旁路
    status = HAL_SD_ConfigWideBusOperation(&s_sd, SDMMC_BUS_WIDE_4B);
    result = Sdmmc_Result(status, HAL_SD_GetError(&s_sd));
    if (result == SYS_OK && (SDMMC1->CLKCR & SDMMC_CLKCR_CLKDIV) != 300U)
        result = SYS_ERR_PROTOCOL;
    if (result == SYS_OK)
    {

        //只对宣告高速能力的卡尝试切换，失败不能冒充默认模式继续使用
        if (s_sd.SdCard.CardSpeed == CARD_HIGH_SPEED || s_sd.SdCard.CardSpeed == CARD_ULTRA_HIGH_SPEED)
        {
            status = HAL_SD_ConfigSpeedBusOperation(&s_sd, SDMMC_SPEED_MODE_HIGH);
            result = Sdmmc_Result(status, HAL_SD_GetError(&s_sd));
            if (result == SYS_OK)
                s_io.high_speed = 1U;
        }
        if (result == SYS_OK)
        {

            //不重配PLL，只调整主机分频；普通卡24MHz，高速卡40MHz
            s_sd.Init.BusWide = SDMMC_BUS_WIDE_4B;
            s_sd.Init.ClockDiv = s_io.high_speed != 0U ? 3U : 5U;
            status = SDMMC_Init(SDMMC1, s_sd.Init);
            result = Sdmmc_Result(status, HAL_SD_GetError(&s_sd));
        }
    }

    //实际CLKCR和CMD13都验证成功后才允许开启IDMA请求
    if (result == SYS_OK)
    {
        state = HAL_SD_GetCardState(&s_sd);
        if (HAL_SD_GetError(&s_sd) != 0U || state != HAL_SD_CARD_TRANSFER ||
            (SDMMC1->CLKCR & SDMMC_CLKCR_CLKDIV) != s_sd.Init.ClockDiv ||
            (SDMMC1->CLKCR & SDMMC_CLKCR_WIDBUS) != SDMMC_BUS_WIDE_4B || !Sdmmc_Quiescent())
            result = SYS_ERR_PROTOCOL;
    }
    if (result == SYS_OK)
    {
        s_io.transfer_ready = 1U;
        s_diagnostics.bus_width = 4U;
        s_diagnostics.clock_div = s_sd.Init.ClockDiv;
        s_diagnostics.sd_clock_hz = SDMMC_KERNEL_HZ / (2U * s_sd.Init.ClockDiv);
        HAL_NVIC_SetPriority(SDMMC1_IRQn, 7U, 0U);
        HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
        HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
    }
    else
    {

        //协商失败后卡状态可能已改变，必须恢复并重新识卡
        s_io.fault = 1U;
        s_io.result = result;
        s_io.hal_error = HAL_SD_GetError(&s_sd);
        s_diagnostics.ready = 0U;
    }
    Sdmmc_Give();
    return result;
}

//读取一个或多个扇区
SysResult_t BSP_SDMMC_ReadBlocks(uint32_t lba, uint32_t count, void *buffer, uint32_t timeout_ms)
{
    return Sdmmc_Transfer(lba, count, buffer, timeout_ms, false);
}

//写入一个或多个扇区
SysResult_t BSP_SDMMC_WriteBlocks(uint32_t lba, uint32_t count, const void *buffer, uint32_t timeout_ms)
{
    //输入只在复制阶段读取，IDMA不会直接访问调用者地址
    return Sdmmc_Transfer(lba, count, (void *)buffer, timeout_ms, true);
}

//获取块操作诊断
SysResult_t BSP_SDMMC_GetIoDiagnostics(BspSdmmcIoDiagnostics_t *diagnostics)
{
    if (diagnostics == NULL)
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;
    *diagnostics = s_io;
    Sdmmc_Give();
    return SYS_OK;
}

//获取本次识卡保存的原始CID
SysResult_t BSP_SDMMC_GetIdentity(uint32_t cid[4])
{
    uint32_t raw;
    bool present;
    SysResult_t result;

    //只复制完整本地身份，不提供跨任务HAL句柄
    if (cid == NULL)
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;
    result = s_diagnostics.ready != 0U ? BSP_SDMMC_ReadDetect(&raw, &present) : SYS_ERR_INVALID_STATE;
    if (result == SYS_OK && !present)
        result = SYS_ERR_NOT_FOUND;
    if (result == SYS_OK)
        memcpy(cid, s_sd.CID, sizeof(s_sd.CID));
    Sdmmc_Give();
    return result;
}

//停止故障外设并撤销旧卡信息
SysResult_t BSP_SDMMC_Recover(void)
{
    SysResult_t result = SYS_OK;

    //恢复只由任务侧执行，不在ISR中等待卡或重置外设
    if (!Sdmmc_CanInit() || s_wait_ops.can_wait == NULL || !s_wait_ops.can_wait(s_wait_ops.context))
        return SYS_ERR_INVALID_STATE;
    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;

    //保留原请求结果，强制复位只用于确保主机不再访问AXI缓冲区
    if (s_hw_ready)
    {
        HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
        SDMMC1->MASK = 0U;
        __HAL_RCC_SDMMC1_FORCE_RESET();
        __DSB();
        __HAL_RCC_SDMMC1_RELEASE_RESET();
        __DSB();
        if (!Sdmmc_Quiescent())
            result = SYS_ERR_IO;
        if (result == SYS_OK)
            result = Sdmmc_Cleanup();
    }
    if (result == SYS_OK)
    {
        s_io.active = 0U;
        s_io.fault = 0U;
        s_io.transfer_ready = 0U;
        s_io.high_speed = 0U;
        s_batch_active = false;
        s_irq_status = 0U;
        s_diagnostics.ready = 0U;
        memset(&s_card, 0, sizeof(s_card));
        HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    }
    s_io.recovery_result = result;
    Sdmmc_Give();
    return result;
}

//记录SDMMC终止事件并唤醒等待任务
void SDMMC1_IRQHandler(void)
{
    uint32_t flags = SDMMC1->STA & SDMMC1->MASK;

    //只接收当前事务的终止事件，不在ISR中执行HAL的CMD12等待
    if (s_batch_active && flags != 0U)
    {
        s_irq_status |= flags;
        s_io.irq_count++;
        SDMMC1->MASK = 0U;
        __DSB();
        if (s_wait_ops.notify != NULL)
            s_wait_ops.notify(s_wait_ops.context);
    }
    else
    {

        //没有活动事务时屏蔽旧事件，不能把它交给下一笔请求
        SDMMC1->MASK = 0U;
    }
}

//确认本批接收完成
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &s_sd && s_batch_active && !s_batch_write)
        s_batch_done = true;
}

//确认本批发送完成，卡内部编程仍需另行等待
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &s_sd && s_batch_active && s_batch_write)
        s_batch_done = true;
}

//锁存原始错误，后续完成回调不能覆盖它
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &s_sd && s_batch_active)
        s_batch_error |= HAL_SD_GetError(hsd) != 0U ? HAL_SD_GetError(hsd) : HAL_SD_ERROR_GENERAL_UNKNOWN_ERR;
}

//Abort通知只表示HAL已走到回调，硬件停止仍由调用者验证
void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
}

//检查同步HAL调用的执行环境
static bool Sdmmc_CanInit(void)
{
    uint32_t basepri = __get_BASEPRI();
    uint32_t priority;

    //关全局中断或处于ISR时，HAL等待Tick可能永远无法结束
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U)
        return false;

    //本工程HAL Tick由TIM6提供，停止或被屏蔽时不能开始轮询
    if ((TIM6->CR1 & TIM_CR1_CEN) == 0U || (TIM6->DIER & TIM_DIER_UIE) == 0U ||
        NVIC_GetEnableIRQ(TIM6_DAC_IRQn) == 0U)
        return false;

    priority = NVIC_GetPriority(TIM6_DAC_IRQn) << (8U - __NVIC_PRIO_BITS);
    return basepri == 0U || (priority != 0U && priority < basepri);
}

//取得本核唯一操作权
static bool Sdmmc_Take(void)
{
    uint32_t primask = __get_PRIMASK();
    bool acquired;

    //只在更新busy时屏蔽本核中断，HAL调用不会在此临界区执行
    __disable_irq();
    acquired = !s_busy;
    if (acquired)
        s_busy = true;

    __set_PRIMASK(primask);
    return acquired;
}

//释放本核操作权
static void Sdmmc_Give(void)
{
    uint32_t primask = __get_PRIMASK();

    //恢复原屏蔽状态，不能擅自开启调用者原本关闭的中断
    __disable_irq();
    s_busy = false;
    __set_PRIMASK(primask);
}

//转换HAL及协议错误
static SysResult_t Sdmmc_Result(HAL_StatusTypeDef status, uint32_t error)
{

    //HAL可能用HAL_ERROR包装超时，必须同时检查错误位
    if (status == HAL_TIMEOUT || (error & (HAL_SD_ERROR_TIMEOUT |
        HAL_SD_ERROR_CMD_RSP_TIMEOUT | HAL_SD_ERROR_DATA_TIMEOUT)) != 0U)
        return SYS_ERR_TIMEOUT;

    if ((error & HAL_SD_ERROR_UNSUPPORTED_FEATURE) != 0U)
        return SYS_ERR_NOT_SUPPORTED;

    if (status == HAL_BUSY)
        return SYS_ERR_BUSY;

    return status == HAL_OK ? SYS_OK : SYS_ERR_IO;
}

//选择现有PLL1Q并核对实际内核时钟
static SysResult_t Sdmmc_Clock(void)
{
    PLL1_ClocksTypeDef clocks;
    RCC_PeriphCLKInitTypeDef clock = {0};
    HAL_StatusTypeDef status;

    //PLL1Q必须已按原系统方案运行，不在这里重配以迁就SD卡
    HAL_RCCEx_GetPLL1ClockFreq(&clocks);
    if ((RCC->CR & RCC_CR_PLL1RDY) == 0U || clocks.PLL1_Q_Frequency != SDMMC_KERNEL_HZ)
        return SYS_ERR_INVALID_STATE;

    //SDMMC1和SDMMC2共用选择器，不能改动已启用SDMMC2的时钟源
    if (__HAL_RCC_SDMMC2_IS_CLK_ENABLED() && __HAL_RCC_GET_SDMMC_SOURCE() != RCC_SDMMCCLKSOURCE_PLL)
        return SYS_ERR_BUSY;

    clock.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
    clock.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
    status = HAL_RCCEx_PeriphCLKConfig(&clock);
    s_diagnostics.rcc_status = status;
    if (status != HAL_OK)
        return Sdmmc_Result(status, 0U);

    //选择成功还需回读，不能只凭HAL_OK认定240MHz已经接入
    s_diagnostics.kernel_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    if (__HAL_RCC_GET_SDMMC_SOURCE() != RCC_SDMMCCLKSOURCE_PLL ||
        s_diagnostics.kernel_clock_hz != SDMMC_KERNEL_HZ)
        return SYS_ERR_INVALID_STATE;

    return SYS_OK;
}

//核对HAL完成后的实际配置
static bool Sdmmc_ConfigValid(void)
{
    uint32_t control = SDMMC1->CLKCR;
    uint32_t div = control & SDMMC_CLKCR_CLKDIV;

    //分频为零会旁路为高频，首版不允许这种配置
    s_diagnostics.clock_div = div;
    s_diagnostics.sd_clock_hz = div != 0U ? s_diagnostics.kernel_clock_hz / (2U * div) :
        s_diagnostics.kernel_clock_hz;
    s_diagnostics.bus_width = (control & SDMMC_CLKCR_WIDBUS) == SDMMC_BUS_WIDE_1B ? 1U : 0U;
    return div == s_sd.Init.ClockDiv && s_diagnostics.sd_clock_hz <= SDMMC_TARGET_HZ &&
        s_diagnostics.bus_width == 1U && SDMMC1->IDMACTRL == 0U && SDMMC1->MASK == 0U;
}

//锁存原始结果并结束初始化操作
static SysResult_t Sdmmc_Finish(SysResult_t result, HAL_StatusTypeDef status, uint32_t started)
{

    //DeInit可能清掉HAL错误，必须先保存失败点的原始状态
    if (s_hw_ready)
    {
        s_diagnostics.status_register = SDMMC1->STA;
        s_diagnostics.clock_register = SDMMC1->CLKCR;
        s_diagnostics.response1 = SDMMC1->RESP1;
    }

    s_diagnostics.hal_status = status;
    s_diagnostics.hal_error = HAL_SD_GetError(&s_sd);
    s_diagnostics.hal_state = HAL_SD_GetState(&s_sd);
    s_diagnostics.init_result = result;
    if (result != SYS_OK)
    {
        s_diagnostics.ready = 0U;
        s_diagnostics.cleanup_result = Sdmmc_Cleanup();
    }

    //无符号相减允许Tick回绕，完成后才向其他调用者开放副本
    s_diagnostics.elapsed_ms = HAL_GetTick() - started;
    Sdmmc_Give();
    return result;
}

//清理已经占用的外设资源
static SysResult_t Sdmmc_Cleanup(void)
{

    //无卡或时钟检查失败时可能尚未进入MSP，不能误操作其他外设
    if (!s_hw_ready)
        return SYS_OK;

    return Sdmmc_Result(HAL_SD_DeInit(&s_sd), 0U);
}

//验证调用者的CPU内存区域
static bool Sdmmc_CpuBufferValid(const void *buffer, uint32_t bytes, bool write)
{
    uint32_t address = (uint32_t)buffer;
    uint32_t end;
    bool valid;

    //拒绝零长度、溢出和专用DMA区，避免memcpy自覆盖或外设寄存器访问
    if (buffer == NULL || bytes == 0U || address > UINT32_MAX - (bytes - 1U))
        return false;
    end = address + bytes - 1U;
    valid = (address >= 0x20000000UL && end < 0x20020000UL) ||
        (address >= 0x24008000UL && end < 0x24080000UL) ||
        (address >= 0x30000000UL && end < 0x30048000UL) ||
        //CPU可读写范围随MPU业务窗口变化，不能继续写死旧4MiB上限
        (address >= BSP_SDRAM_BASE && end < BSP_SDRAM_END);

    //写卡允许从Flash读取常量，读卡不能把Flash当作可写目的区
    if (write && address >= 0x08000000UL && end < 0x08200000UL)
        valid = true;
    return valid;
}

//按地址类型执行受控复制
static void Sdmmc_CopyBytes(uint8_t *destination, const uint8_t *source, uint32_t bytes)
{
    const volatile uint8_t *read_byte = source;
    volatile uint8_t *write_byte = destination;
    uint32_t index;

    //SDRAM保持逐字节访问，避免编译器合并为库函数或宽访问
    if ((uint32_t)source >= BSP_SDRAM_BASE && (uint32_t)source < BSP_SDRAM_END &&
        bytes <= BSP_SDRAM_END - (uint32_t)source)
    {
        for (index = 0U; index < bytes; index++)
            write_byte[index] = read_byte[index];
    }
    else
    {

        //内部RAM仍使用原复制方式，不改变文件头等输入路径
        memcpy(destination, source, bytes);
    }

    //复制完成后再交给原有硬件传输流程
    __DMB();
}

//准备文件系统使用的小块内部输入
SysResult_t BSP_SDMMC_PrepareWriteInput(void *destination, const void *source, uint32_t bytes)
{
#if SDMMC_COPY_GAP_MS != 0U
    uint32_t started, elapsed, waits = 0U;
    SysResult_t result;
#endif

    //独立输入缓冲不能与源或私有IDMA缓冲重叠，也不能越过已映射内存
    if (bytes == 0U || bytes > SDMMC_COPY_CHUNK_BYTES ||
        !Sdmmc_CpuBufferValid(source, bytes, true) ||
        !Sdmmc_CpuBufferValid(destination, bytes, false))
        return SYS_ERR_INVALID_ARG;
    if ((uint32_t)source <= (uint32_t)destination + bytes - 1U &&
        (uint32_t)destination <= (uint32_t)source + bytes - 1U)
        return SYS_ERR_INVALID_ARG;

    //文件系统已持有任务侧所有权，复制仍要求可阻塞环境且IDMA已经停止
    if (!Sdmmc_CanInit() || s_wait_ops.can_wait == NULL ||
        !s_wait_ops.can_wait(s_wait_ops.context))
        return SYS_ERR_INVALID_STATE;
    if (s_busy || s_io.active != 0U || !Sdmmc_Quiescent())
        return SYS_ERR_BUSY;

#if SDMMC_COPY_GAP_MS != 0U

    //只为SDRAM输入保留1ms间隔，内部RAM的文件头不额外等待
    if ((uint32_t)source >= BSP_SDRAM_BASE && (uint32_t)source < BSP_SDRAM_END)
    {
        started = HAL_GetTick();
        while ((elapsed = HAL_GetTick() - started) < SDMMC_COPY_GAP_MS)
        {

            //残留通知不代表间隔已到，有限重试避免时钟异常时空转
            if (waits++ >= 16U)
                return SYS_ERR_TIMEOUT;
            result = s_wait_ops.wait(s_wait_ops.context, SDMMC_COPY_GAP_MS - elapsed);
            if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
                return result;
        }
    }
#endif

    //继续使用同一份首次故障标记，SDRAM读取实际执行逐字节访问
    Sdmmc_CopyBytes(destination, source, bytes);
    return SYS_OK;
}

//分段复制SDRAM数据到AXI缓冲
static SysResult_t Sdmmc_CopyToAxi(const uint8_t *source, uint32_t bytes,
    uint32_t started, uint32_t timeout_ms)
{
    uint32_t copied = 0U, amount;
#if SDMMC_COPY_GAP_MS != 0U
    uint32_t gap_started, elapsed, waits;
    SysResult_t result;
#endif

    //内部调用仍核对容量，防止分段时写出专用AXI缓冲
    if (source == NULL || bytes == 0U || bytes > BSP_SDMMC_DMA_BYTES)
        return SYS_ERR_INVALID_ARG;

    //只有SDRAM源参加实验，文件头等内部RAM数据保持原复制方式
    if ((uint32_t)source < BSP_SDRAM_BASE || (uint32_t)source >= BSP_SDRAM_END ||
        bytes > BSP_SDRAM_END - (uint32_t)source)
    {
        Sdmmc_CopyBytes(g_sdmmc_dma_buffer, source, bytes);
        return SYS_OK;
    }

    while (copied < bytes)
    {

        //复制和段间等待都使用原请求的总超时，不重新计算截止时间
        if (HAL_GetTick() - started >= timeout_ms)
            return SYS_ERR_TIMEOUT;
        amount = bytes - copied;
        if (amount > SDMMC_COPY_CHUNK_BYTES)
            amount = SDMMC_COPY_CHUNK_BYTES;
        Sdmmc_CopyBytes(g_sdmmc_dma_buffer + copied, source + copied, amount);
        copied += amount;

#if SDMMC_COPY_GAP_MS != 0U

        //最后一段后直接交给IDMA，不额外增加一轮等待
        if (copied == bytes)
            break;
        gap_started = HAL_GetTick();
        waits = 0U;
        while ((elapsed = HAL_GetTick() - gap_started) < SDMMC_COPY_GAP_MS)
        {

            //残留通知可能立即返回，继续等到本地Tick确认已间隔1ms
            if (HAL_GetTick() - started >= timeout_ms || waits++ >= 16U)
                return SYS_ERR_TIMEOUT;
            result = s_wait_ops.wait(s_wait_ops.context, SDMMC_COPY_GAP_MS - elapsed);

            //正常唤醒和等待到期都可继续，其他等待错误保留原始结果
            if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
                return result;
        }
#endif
    }

    //总超时已经到期时禁止再启动新的IDMA批次
    return HAL_GetTick() - started >= timeout_ms ? SYS_ERR_TIMEOUT : SYS_OK;
}

//确认本次IDMA已不再访问AXI缓冲区
static bool Sdmmc_Quiescent(void)
{
    __DSB();
    return (SDMMC1->IDMACTRL & SDMMC_IDMA_IDMAEN) == 0U &&
        (SDMMC1->STA & (SDMMC_FLAG_DPSMACT | SDMMC_FLAG_CMDACT)) == 0U &&
        (SDMMC1->CMD & SDMMC_CMD_CMDTRANS) == 0U;
}

//处理真实硬件终止状态
static void Sdmmc_ServiceEvents(void)
{
    uint32_t primask, notified, flags, errors;

    //IRQ和主循环之间只交换事件，不在临界区内运行HAL
    primask = __get_PRIMASK();
    __disable_irq();
    notified = s_irq_status;
    s_irq_status = 0U;
    flags = SDMMC1->STA;
    __set_PRIMASK(primask);
    errors = flags & (SDMMC_FLAG_DCRCFAIL | SDMMC_FLAG_DTIMEOUT |
        SDMMC_FLAG_RXOVERR | SDMMC_FLAG_TXUNDERR | SDMMC_FLAG_IDMATE);
    if ((flags & SDMMC_FLAG_DATAEND) == 0U && errors == 0U)
        return;

    //先保存错误，因为当前HAL先处理DATAEND，可能清除同周期错误标志
    if (errors != 0U)
    {
        if ((errors & SDMMC_FLAG_DTIMEOUT) != 0U)
            s_batch_error |= HAL_SD_ERROR_DATA_TIMEOUT;
        if ((errors & SDMMC_FLAG_DCRCFAIL) != 0U)
            s_batch_error |= HAL_SD_ERROR_DATA_CRC_FAIL;
        if ((errors & SDMMC_FLAG_RXOVERR) != 0U)
            s_batch_error |= HAL_SD_ERROR_RX_OVERRUN;
        if ((errors & SDMMC_FLAG_TXUNDERR) != 0U)
            s_batch_error |= HAL_SD_ERROR_TX_UNDERRUN;
        if ((errors & SDMMC_FLAG_IDMATE) != 0U)
            s_batch_error |= HAL_SD_ERROR_DMA;
    }
    if (notified == 0U)
        s_io.lost_irq_polls++;
    s_io.status = flags;
    SDMMC1->MASK = 0U;

    //IDMA错误不由当前HAL IRQ覆盖，交给任务侧统一Abort
    if ((errors & SDMMC_FLAG_IDMATE) == 0U)
        HAL_SD_IRQHandler(&s_sd);
    __DSB();
}

//等待传输完成和卡恢复可传输状态
static SysResult_t Sdmmc_WaitBatch(uint32_t started, uint32_t timeout_ms)
{
    uint32_t raw, elapsed, state;
    bool present;
    SysResult_t result;

    for (;;)
    {

        //卡拔出和时间到期都优先于成功，避免交付已经过期的结果
        result = BSP_SDMMC_ReadDetect(&raw, &present);
        if (result != SYS_OK || !present)
            return result != SYS_OK ? result : SYS_ERR_NOT_FOUND;
        elapsed = HAL_GetTick() - started;
        if (elapsed >= timeout_ms)
            return SYS_ERR_TIMEOUT;
        Sdmmc_ServiceEvents();
        if (s_batch_error != 0U || HAL_SD_GetError(&s_sd) != 0U)
            return Sdmmc_Result(HAL_ERROR, s_batch_error | HAL_SD_GetError(&s_sd));

        //HAL回调、主机停止和卡TRANSFER都满足才能复用本批DMA内存
        if (s_batch_done && Sdmmc_Quiescent() && HAL_SD_GetState(&s_sd) == HAL_SD_STATE_READY)
        {
            //DATAEND之外再核对剩余字节，拒绝完成回调与计数器矛盾的帧
            if (SDMMC1->DCOUNT != 0U)
                return SYS_ERR_PROTOCOL;
            state = HAL_SD_GetCardState(&s_sd);
            if (HAL_SD_GetError(&s_sd) != 0U)
                return Sdmmc_Result(HAL_ERROR, HAL_SD_GetError(&s_sd));
            if (HAL_GetTick() - started >= timeout_ms)
                return SYS_ERR_TIMEOUT;
            if (state == HAL_SD_CARD_TRANSFER)
                return SYS_OK;
        }

        //有限阻塞还可发现丢失IRQ，等待期间其他业务任务正常运行
        elapsed = HAL_GetTick() - started;
        if (elapsed >= timeout_ms)
            return SYS_ERR_TIMEOUT;
        result = s_wait_ops.wait(s_wait_ops.context, timeout_ms - elapsed > 5U ? 5U : timeout_ms - elapsed);
        if (result != SYS_OK && result != SYS_ERR_TIMEOUT)
            return result;
    }
}

//停止失败的IDMA事务
static SysResult_t Sdmmc_StopIo(void)
{
    HAL_StatusTypeDef status;
    SysResult_t result;

    //屏蔽事件后才停止硬件，清理期间不允许旧IRQ改变事务
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    SDMMC1->MASK = 0U;
    status = HAL_SD_Abort(&s_sd);
    result = Sdmmc_Result(status, 0U);
    if (result == SYS_OK && !Sdmmc_Quiescent())
        result = SYS_ERR_IO;
    if (result == SYS_OK)
    {
        __HAL_SD_CLEAR_FLAG(&s_sd, SDMMC_STATIC_FLAGS);
        HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
        s_batch_active = false;
        s_irq_status = 0U;
        s_io.active = 0U;
    }

    //无论停止是否成功，原卡状态都不能直接继续使用
    s_io.fault = 1U;
    s_io.transfer_ready = 0U;
    s_diagnostics.ready = 0U;
    return result;
}

//执行带统一截止时间的分批块读写
static SysResult_t Sdmmc_Transfer(uint32_t lba, uint32_t count, void *buffer,
    uint32_t timeout_ms, bool write)
{
    uint32_t started, done = 0U, blocks, bytes, primask;
    uint32_t raw;
    bool present;
    SysResult_t result;
    HAL_StatusTypeDef status;

    //使用减法校验范围，避免HAL中lba+count产生无符号回绕
    if (count == 0U || count > UINT32_MAX / BSP_SDMMC_BLOCK_SIZE ||
        timeout_ms == 0U || timeout_ms > INT32_MAX ||
        !Sdmmc_CpuBufferValid(buffer, count * BSP_SDMMC_BLOCK_SIZE, write))
        return SYS_ERR_INVALID_ARG;
    if (!Sdmmc_CanInit() || s_wait_ops.can_wait == NULL ||
        !s_wait_ops.can_wait(s_wait_ops.context) || !BSP_SDMMC_MemoryValid())
        return SYS_ERR_INVALID_STATE;
    if (!Sdmmc_Take())
        return SYS_ERR_BUSY;
    if (s_io.transfer_ready == 0U || s_io.fault != 0U || s_diagnostics.ready == 0U)
    {
        Sdmmc_Give();
        return SYS_ERR_INVALID_STATE;
    }
    //共享时钟或寄存器被其他代码改写后拒绝传输，不能沿用旧40MHz诊断
    if (__HAL_RCC_GET_SDMMC_SOURCE() != RCC_SDMMCCLKSOURCE_PLL ||
        HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC) != SDMMC_KERNEL_HZ ||
        (SDMMC1->CLKCR & SDMMC_CLKCR_WIDBUS) != SDMMC_BUS_WIDE_4B ||
        (SDMMC1->CLKCR & SDMMC_CLKCR_CLKDIV) != (s_io.high_speed != 0U ? 3U : 5U))
    {
        s_io.fault = 1U;
        s_io.transfer_ready = 0U;
        s_diagnostics.ready = 0U;
        Sdmmc_Give();
        return SYS_ERR_INVALID_STATE;
    }
    if (lba >= s_card.logical_block_count || count > s_card.logical_block_count - lba ||
        (s_card.card_type == CARD_SDSC && lba + count - 1U > UINT32_MAX / BSP_SDMMC_BLOCK_SIZE))
    {
        Sdmmc_Give();
        return SYS_ERR_INVALID_ARG;
    }

    //取得唯一操作权后更新请求诊断，不重置历史IRQ计数
    started = HAL_GetTick();
    s_io.lba = lba;
    s_io.requested_blocks = count;
    s_io.completed_blocks = 0U;
    s_io.write = write ? 1U : 0U;
    s_io.result = SYS_OK;
    s_io.recovery_result = SYS_OK;
    s_io.hal_error = 0U;
    s_io.active = 1U;
    s_io.dma_address = (uint32_t)g_sdmmc_dma_buffer;
    s_io.dma_bytes = sizeof(g_sdmmc_dma_buffer);
    result = SYS_OK;
    while (done < count)
    {

        //每批都重新检查卡和剩余时间，不在大请求中无限续期
        result = BSP_SDMMC_ReadDetect(&raw, &present);
        if (result != SYS_OK || !present)
        {
            result = result != SYS_OK ? result : SYS_ERR_NOT_FOUND;
            break;
        }
        if (HAL_GetTick() - started >= timeout_ms)
        {
            result = SYS_ERR_TIMEOUT;
            break;
        }
        if (!Sdmmc_Quiescent() || HAL_SD_GetState(&s_sd) != HAL_SD_STATE_READY)
        {
            result = SYS_ERR_INVALID_STATE;
            break;
        }
        blocks = count - done > BSP_SDMMC_DMA_BLOCKS ? BSP_SDMMC_DMA_BLOCKS : count - done;
        bytes = blocks * BSP_SDMMC_BLOCK_SIZE;
        if (write)
        {

            //仅调整SDRAM复制节奏，必须整批准备完毕后才启动原IDMA传输
            result = Sdmmc_CopyToAxi((const uint8_t *)buffer + done * BSP_SDMMC_BLOCK_SIZE,
                bytes, started, timeout_ms);
            if (result != SYS_OK)
                break;
        }

        //CPU准备完成后建立本批状态，清除旧事件再交给IDMA
        primask = __get_PRIMASK();
        __disable_irq();
        SDMMC1->MASK = 0U;
        __HAL_SD_CLEAR_FLAG(&s_sd, SDMMC_STATIC_FLAGS);
        HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
        s_irq_status = 0U;
        s_batch_done = false;
        s_batch_error = 0U;
        s_batch_write = write;
        s_batch_active = true;
        __DSB();
        __set_PRIMASK(primask);
        status = write ? HAL_SD_WriteBlocks_DMA(&s_sd, g_sdmmc_dma_buffer, lba + done, blocks) :
            HAL_SD_ReadBlocks_DMA(&s_sd, g_sdmmc_dma_buffer, lba + done, blocks);
        result = Sdmmc_Result(status, HAL_SD_GetError(&s_sd));
        if (result != SYS_OK)
            break;

        //IDMATE没有独立MASK使能位，由有限等待后的状态检查捕获
        result = Sdmmc_WaitBatch(started, timeout_ms);
        if (result != SYS_OK)
            break;
        s_batch_active = false;
        __DSB();
        if (!write)
            memcpy((uint8_t *)buffer + done * BSP_SDMMC_BLOCK_SIZE, g_sdmmc_dma_buffer, bytes);
        done += blocks;
        s_io.completed_blocks = done;
    }

    //先保存原错误，再尝试停止；清理结果不能覆盖原操作结果
    s_io.result = result;
    s_io.hal_error = s_batch_error | HAL_SD_GetError(&s_sd);
    if (result != SYS_OK)
        s_io.recovery_result = Sdmmc_StopIo();
    else
        s_io.active = 0U;
    s_io.elapsed_ms = HAL_GetTick() - started;
    Sdmmc_Give();
    return result;
}
