//SDMMC1只负责低速1-bit识卡与诊断，不接文件系统或视频链路
#include "bsp_sdmmc.h"
#include "stm32h7xx_hal.h"
#include <stddef.h>
#include <string.h>

//复用摄像头已经启用的PLL1Q，不修改PLL参数
#define SDMMC_KERNEL_HZ 240000000UL

//首版初始化及识卡完成后都保持不高于400kHz
#define SDMMC_TARGET_HZ 400000UL

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

    //R49提供外部上拉，不用内部下拉改变卡座开关电平
    __HAL_RCC_GPIOH_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_14;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
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
    if (s_hw_ready)
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
            result = SYS_ERR_NOT_FOUND;

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

    __HAL_RCC_SDMMC1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
    s_hw_ready = false;
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
