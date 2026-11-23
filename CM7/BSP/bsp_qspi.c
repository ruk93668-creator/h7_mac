#include "bsp_qspi.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#if defined(QSPI_H1_BACKUP) || defined(QSPI_H1_BACKUP120) || defined(QSPI_H1_WRITE)
#include "../../Tests/QspiFlashQ1/hardware/qspi_h1.h"
#define QSPI_TARGET_HZ H1_CLOCK_HZ
#elif defined(QSPI_QUAD_ID_DIAGNOSTIC)
#include "../../Tests/QspiFlashQ1/hardware/qspi_quad_id.h"
#if defined(QSPI_SFDP_DIAGNOSTIC)
#error Select only one QSPI diagnostic
#endif
#define QSPI_TARGET_HZ QSPI_QUAD_ID_DIAGNOSTIC
#elif defined(QSPI_SFDP_DIAGNOSTIC)
#include "../../Tests/QspiFlashQ1/hardware/qspi_sfdp_diagnostic.h"
#define QSPI_TARGET_HZ 30000000UL
#else
#define QSPI_TARGET_HZ 120000000UL
#endif
//首次 HAL Init 强制使用 5000ms；预算另留准备/识别余量，不能虚报为短超时。
#define QSPI_INIT_BUDGET_MS (HAL_QSPI_TIMEOUT_DEFAULT_VALUE + 200U)
#define QSPI_IO_TIMEOUT_MS 100U
#define QSPI_F_PINS (GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10)
#define QSPI_G_PINS GPIO_PIN_6
#define QSPI_F_MASK 0x003FF000UL
#define QSPI_G_MASK 0x00003000UL

typedef struct
{
    uint32_t address, bytes;
} QspiWritePolicy_t;

//当前没有获批测试扇区。公共写入入口只能使用这个不可变策略。
static const QspiWritePolicy_t s_write_policy = {0U, 0U};

static QSPI_HandleTypeDef s_qspi;
static BspQspiDiagnostics_t s_diag;
static BspQspiFaultDiagnostics_t s_fault;
static BspQspiCleanupStage_t s_cleanup_stage;
static bool s_busy;
static bool s_owned;
static bool s_hal_attempted;
//只保存所属引脚位；恢复时不能覆盖 SDRAM/DCMI 的共享端口配置。
typedef struct
{
    uint32_t mode, type, speed, pull, af;
} QspiPins_t;
static QspiPins_t s_f, s_g;
static uint32_t s_clock_source;
static uint32_t s_f_af_high;

static bool Qspi_ContextValid(void)
{
    uint32_t priority, basepri = __get_BASEPRI();
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U || __get_FAULTMASK() != 0U)
        return false;
    if ((TIM6->CR1 & TIM_CR1_CEN) == 0U || (TIM6->DIER & TIM_DIER_UIE) == 0U ||
        NVIC_GetEnableIRQ(TIM6_DAC_IRQn) == 0U)
        return false;
    priority = NVIC_GetPriority(TIM6_DAC_IRQn) << (8U - __NVIC_PRIO_BITS);
    return basepri == 0U || (priority != 0U && priority < basepri);
}

static bool Qspi_Take(void)
{
    uint32_t mask = __get_PRIMASK();
    bool acquired;
    __disable_irq();
    acquired = !s_busy;
    if (acquired)
        s_busy = true;
    __set_PRIMASK(mask);
    return acquired;
}

static void Qspi_Give(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    s_busy = false;
    __set_PRIMASK(mask);
}

static void Qspi_RecordFault(BspQspiFaultOrigin_t origin, uint32_t stage, SysResult_t result,
    uint32_t address, uint32_t opcode, uint32_t hal_status)
{
    //已锁存时保留首错，后续清理和恢复只能更新各自的证据。
    if (s_diag.fault != 0U)
        return;
    s_fault.fault_origin = origin;
    s_fault.fault_stage = stage;
    s_fault.primary_result = result;
    s_fault.cleanup_result = s_fault.last_cleanup_result = SYS_OK;
    s_fault.cleanup_stage = s_fault.last_cleanup_stage = BSP_QSPI_CLEANUP_NONE;
    s_fault.cleanup_attempted = 0U;
    s_fault.fault_address = address;
    s_fault.fault_opcode = opcode;
    s_fault.hal_status = hal_status;
    s_fault.hal_error = s_qspi.ErrorCode;
    s_fault.hal_state = s_qspi.State;
    s_fault.flash_completion_unknown = s_fault.wren_possible = s_fault.mutation_possible = 0U;
    s_fault.controller_idle_verified = s_fault.flash_idle_verified = 0U;
    s_fault.recovery_result = SYS_OK;
    s_fault.recovery_stage = BSP_QSPI_RECOVERY_NONE;
    s_fault.recovery_elapsed_ms = 0U;
}

static void Qspi_RecordCleanup(SysResult_t result)
{
    if (s_fault.cleanup_attempted == 0U)
    {
        s_fault.cleanup_result = result;
        s_fault.cleanup_stage = s_cleanup_stage;
        s_fault.cleanup_attempted = 1U;
    }
    s_fault.last_cleanup_result = result;
    s_fault.last_cleanup_stage = s_cleanup_stage;
}

static void Qspi_SavePins(GPIO_TypeDef *gpio, QspiPins_t *save)
{
    save->mode = gpio->MODER;
    save->type = gpio->OTYPER;
    save->speed = gpio->OSPEEDR;
    save->pull = gpio->PUPDR;
    save->af = gpio->AFR[0];
}

static bool Qspi_PinValid(GPIO_TypeDef *gpio, uint32_t pin, uint32_t af)
{
    return ((gpio->MODER >> (pin * 2U)) & 3U) == 2U &&
        ((gpio->OTYPER >> pin) & 1U) == 0U &&
        ((gpio->OSPEEDR >> (pin * 2U)) & 3U) == GPIO_SPEED_FREQ_VERY_HIGH &&
        ((gpio->PUPDR >> (pin * 2U)) & 3U) == GPIO_NOPULL &&
        ((gpio->AFR[pin / 8U] >> ((pin % 8U) * 4U)) & 15U) == af;
}

static bool Qspi_HardwareValid(void)
{
    return s_owned && __HAL_RCC_QSPI_IS_CLK_ENABLED() &&
        __HAL_RCC_GPIOF_IS_CLK_ENABLED() && __HAL_RCC_GPIOG_IS_CLK_ENABLED() &&
        __HAL_RCC_GET_QSPI_SOURCE() == RCC_QSPICLKSOURCE_D1HCLK &&
        HAL_RCC_GetHCLKFreq() == s_diag.kernel_clock_hz &&
        Qspi_PinValid(GPIOF, 6U, GPIO_AF9_QUADSPI) &&
        Qspi_PinValid(GPIOF, 7U, GPIO_AF9_QUADSPI) &&
        Qspi_PinValid(GPIOF, 8U, GPIO_AF10_QUADSPI) &&
        Qspi_PinValid(GPIOF, 9U, GPIO_AF10_QUADSPI) &&
        Qspi_PinValid(GPIOF, 10U, GPIO_AF9_QUADSPI) &&
        Qspi_PinValid(GPIOG, 6U, GPIO_AF10_QUADSPI);
}

static SysResult_t Qspi_PrepareHardware(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t div, frequency = HAL_RCC_GetHCLKFreq();
    //已启用的外设可能属于其他调用者，不允许复位或抢占它。
    if (__HAL_RCC_QSPI_IS_CLK_ENABLED())
        return SYS_ERR_BUSY;
    if (frequency == 0U)
        return SYS_ERR_INVALID_STATE;
    div = frequency / QSPI_TARGET_HZ + (frequency % QSPI_TARGET_HZ != 0U ? 1U : 0U);
    if (div == 0U || div > 256U)
        return SYS_ERR_NOT_SUPPORTED;
    s_diag.kernel_clock_hz = frequency;
    s_diag.prescaler = div - 1U;
    s_diag.serial_clock_hz = frequency / div;
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    Qspi_SavePins(GPIOF, &s_f);
    Qspi_SavePins(GPIOG, &s_g);
    //PF8/9/10 的 AF 位位于 AFR[1]，与低组分开保存。
    s_f.af = GPIOF->AFR[0];
    s_f_af_high = GPIOF->AFR[1];
    s_clock_source = __HAL_RCC_GET_QSPI_SOURCE();
    s_owned = true;
    __HAL_RCC_QSPI_CONFIG(RCC_QSPICLKSOURCE_D1HCLK);
    __HAL_RCC_QSPI_CLK_ENABLE();
    //只复位已独占的 QUADSPI 控制器，清除遗留 DMA/IRQ/映射配置，不复位 Flash。
    __HAL_RCC_QSPI_FORCE_RESET();
    __HAL_RCC_QSPI_RELEASE_RESET();
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_10;
    gpio.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio);
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOF, &gpio);
    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOG, &gpio);
    return Qspi_HardwareValid() ? SYS_OK : SYS_ERR_IO;
}

//MSP 无返回值：只旁路保存本轮核验，不能依赖 HAL 的 ErrorCode 传播失败。
void HAL_QSPI_MspInit(QSPI_HandleTypeDef *handle)
{
    if (handle != &s_qspi || handle->Instance != QUADSPI)
        return;
    s_diag.msp_called++;
    s_diag.msp_result = s_diag.prepare_result == SYS_OK && Qspi_HardwareValid() ?
        SYS_OK : SYS_ERR_INVALID_STATE;
}

static bool Qspi_ConfigValid(void)
{
    uint32_t cr_mask = QUADSPI_CR_PRESCALER | QUADSPI_CR_SSHIFT |
        QUADSPI_CR_FSEL | QUADSPI_CR_DFM | QUADSPI_CR_FTHRES | QUADSPI_CR_EN |
        QUADSPI_CR_DMAEN | QUADSPI_CR_TEIE | QUADSPI_CR_TCIE | QUADSPI_CR_FTIE |
        QUADSPI_CR_SMIE | QUADSPI_CR_TOIE;
    uint32_t dcr_mask = QUADSPI_DCR_FSIZE | QUADSPI_DCR_CSHT | QUADSPI_DCR_CKMODE;
    return Qspi_HardwareValid() &&
        (QUADSPI->CR & cr_mask) ==
            ((s_diag.prescaler << QUADSPI_CR_PRESCALER_Pos) | QSPI_SAMPLE_SHIFTING_HALFCYCLE | QUADSPI_CR_EN) &&
        (QUADSPI->DCR & dcr_mask) == ((23U << QUADSPI_DCR_FSIZE_Pos) | QSPI_CS_HIGH_TIME_8_CYCLE) &&
        (QUADSPI->SR & QUADSPI_SR_BUSY) == 0U;
}

static SysResult_t Qspi_Result(HAL_StatusTypeDef status)
{
    if (status == HAL_TIMEOUT || (s_qspi.ErrorCode & HAL_QSPI_ERROR_TIMEOUT) != 0U)
        return SYS_ERR_TIMEOUT;
    if (status == HAL_BUSY)
        return SYS_ERR_BUSY;
    return status == HAL_OK && s_qspi.ErrorCode == HAL_QSPI_ERROR_NONE ? SYS_OK : SYS_ERR_IO;
}

static void Qspi_RestorePins(GPIO_TypeDef *gpio, const QspiPins_t *save,
    uint32_t pins, uint32_t mask, uint32_t af_mask)
{
    MODIFY_REG(gpio->MODER, mask, save->mode & mask);
    MODIFY_REG(gpio->OTYPER, pins, save->type & pins);
    MODIFY_REG(gpio->OSPEEDR, mask, save->speed & mask);
    MODIFY_REG(gpio->PUPDR, mask, save->pull & mask);
    MODIFY_REG(gpio->AFR[0], af_mask, save->af & af_mask);
}

static SysResult_t Qspi_Cleanup(void)
{
    HAL_StatusTypeDef status;
    uint32_t started = HAL_GetTick();
    if (!s_owned)
        return SYS_OK;
    if (!Qspi_ContextValid() || !__HAL_RCC_QSPI_IS_CLK_ENABLED())
        return SYS_ERR_INVALID_STATE;
    if (s_hal_attempted)
    {
        s_qspi.Timeout = QSPI_IO_TIMEOUT_MS;
        status = HAL_QSPI_Abort(&s_qspi);
        if (status != HAL_OK || (QUADSPI->SR & QUADSPI_SR_BUSY) != 0U ||
            HAL_GetTick() - started > QSPI_IO_TIMEOUT_MS + 1U)
            return SYS_ERR_IO;
        status = HAL_QSPI_DeInit(&s_qspi);
        if (status != HAL_OK || (QUADSPI->CR & QUADSPI_CR_EN) != 0U)
            return SYS_ERR_IO;
    }
    //不关 GPIOF/G 时钟，只归还本模块保存的引脚位和 QSPI mux。
    Qspi_RestorePins(GPIOF, &s_f, QSPI_F_PINS, QSPI_F_MASK, 0xFF000000UL);
    MODIFY_REG(GPIOF->AFR[1], 0x00000FFFUL, s_f_af_high & 0x00000FFFUL);
    Qspi_RestorePins(GPIOG, &s_g, QSPI_G_PINS, QSPI_G_MASK, 0x0F000000UL);
    __HAL_RCC_QSPI_CONFIG(s_clock_source);
    __HAL_RCC_QSPI_CLK_DISABLE();
    if (__HAL_RCC_QSPI_IS_CLK_ENABLED() || __HAL_RCC_GET_QSPI_SOURCE() != s_clock_source ||
        (GPIOF->MODER & QSPI_F_MASK) != (s_f.mode & QSPI_F_MASK) ||
        (GPIOG->MODER & QSPI_G_MASK) != (s_g.mode & QSPI_G_MASK) ||
        (GPIOF->OTYPER & QSPI_F_PINS) != (s_f.type & QSPI_F_PINS) ||
        (GPIOG->OTYPER & QSPI_G_PINS) != (s_g.type & QSPI_G_PINS) ||
        (GPIOF->OSPEEDR & QSPI_F_MASK) != (s_f.speed & QSPI_F_MASK) ||
        (GPIOG->OSPEEDR & QSPI_G_MASK) != (s_g.speed & QSPI_G_MASK) ||
        (GPIOF->PUPDR & QSPI_F_MASK) != (s_f.pull & QSPI_F_MASK) ||
        (GPIOG->PUPDR & QSPI_G_MASK) != (s_g.pull & QSPI_G_MASK) ||
        (GPIOF->AFR[0] & 0xFF000000UL) != (s_f.af & 0xFF000000UL) ||
        (GPIOF->AFR[1] & 0xFFFUL) != (s_f_af_high & 0xFFFUL) ||
        (GPIOG->AFR[0] & 0x0F000000UL) != (s_g.af & 0x0F000000UL))
        return SYS_ERR_IO;
    s_owned = false;
    s_hal_attempted = false;
    return SYS_OK;
}

static void Qspi_Snapshot(void)
{
    s_diag.hal_error = s_qspi.ErrorCode;
    s_diag.hal_state = s_qspi.State;
    if (s_owned && __HAL_RCC_QSPI_IS_CLK_ENABLED())
    {
        s_diag.cr = QUADSPI->CR;
        s_diag.dcr = QUADSPI->DCR;
        s_diag.sr = QUADSPI->SR;
    }
}

static SysResult_t Qspi_ReadId(uint32_t started)
{
    QSPI_CommandTypeDef command = {0};
    HAL_StatusTypeDef status;
    uint8_t id[3] = {0};
    uint32_t elapsed, timeout;
    command.Instruction = 0x9FU;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = QSPI_ADDRESS_NONE;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = QSPI_DATA_1_LINE;
    command.NbData = sizeof(id);
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    elapsed = HAL_GetTick() - started;
    if (elapsed >= QSPI_INIT_BUDGET_MS)
        return SYS_ERR_TIMEOUT;
    timeout = QSPI_INIT_BUDGET_MS - elapsed;
    if (timeout > QSPI_IO_TIMEOUT_MS)
        timeout = QSPI_IO_TIMEOUT_MS;
    s_diag.stage = BSP_QSPI_STAGE_COMMAND;
    status = HAL_QSPI_Command(&s_qspi, &command, timeout);
    s_diag.hal_status = status;
    if (Qspi_Result(status) != SYS_OK)
        return Qspi_Result(status);
    //HAL Receive 由 DLR 决定写入长度，先核验它，避免错误配置越过三字节缓冲。
    if (QUADSPI->DLR != sizeof(id) - 1U)
        return SYS_ERR_PROTOCOL;
    elapsed = HAL_GetTick() - started;
    if (elapsed >= QSPI_INIT_BUDGET_MS || !Qspi_ContextValid())
        return SYS_ERR_TIMEOUT;
    timeout = QSPI_INIT_BUDGET_MS - elapsed;
    if (timeout > QSPI_IO_TIMEOUT_MS)
        timeout = QSPI_IO_TIMEOUT_MS;
    s_diag.stage = BSP_QSPI_STAGE_RECEIVE;
    status = HAL_QSPI_Receive(&s_qspi, id, timeout);
    s_diag.hal_status = status;
    s_diag.received_bytes = s_qspi.RxXferSize >= s_qspi.RxXferCount ?
        s_qspi.RxXferSize - s_qspi.RxXferCount : 0U;
    if (Qspi_Result(status) != SYS_OK)
        return Qspi_Result(status);
    if (HAL_GetTick() - started >= QSPI_INIT_BUDGET_MS)
        return SYS_ERR_TIMEOUT;
    if (s_qspi.RxXferSize != 3U || s_qspi.RxXferCount != 0U)
        return SYS_ERR_PROTOCOL;
    s_diag.jedec_id = ((uint32_t)id[0] << 16) | ((uint32_t)id[1] << 8) | id[2];
    return s_diag.jedec_id == BSP_QSPI_EXPECTED_ID ? SYS_OK : SYS_ERR_PROTOCOL;
}

SysResult_t BSP_QSPI_Init(void)
{
    SysResult_t result;
    HAL_StatusTypeDef status;
    uint32_t started, cleanup_started, attempts;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    if (s_diag.fault != 0U || s_diag.ready != 0U)
    {
        result = s_diag.fault != 0U ? SYS_ERR_INVALID_STATE : SYS_ERR_ALREADY_EXISTS;
        Qspi_Give();
        return result;
    }
    attempts = s_diag.attempts + 1U;
    memset(&s_diag, 0, sizeof(s_diag));
    memset(&s_qspi, 0, sizeof(s_qspi));
    s_diag.attempts = attempts;
    s_diag.msp_result = SYS_ERR_INVALID_STATE;
    s_diag.init_timeout_ms = HAL_QSPI_TIMEOUT_DEFAULT_VALUE;
    s_diag.stage = BSP_QSPI_STAGE_PREPARE;
    started = HAL_GetTick();
    result = Qspi_PrepareHardware();
    s_diag.prepare_result = result;
    if (result == SYS_OK && HAL_GetTick() - started >= 100U)
        result = SYS_ERR_TIMEOUT;
    if (result == SYS_OK)
    {
        s_qspi.Instance = QUADSPI;
        s_qspi.Init.ClockPrescaler = s_diag.prescaler;
        s_qspi.Init.FifoThreshold = 1U;
        //高速读取延后半周期采样，并在初始化后核对 SSHIFT；稳定性由实板复测确认。
        s_qspi.Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
        s_qspi.Init.FlashSize = 23U;
        s_qspi.Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_8_CYCLE;
        s_qspi.Init.ClockMode = QSPI_CLOCK_MODE_0;
        s_qspi.Init.FlashID = QSPI_FLASH_ID_1;
        s_qspi.Init.DualFlash = QSPI_DUALFLASH_DISABLE;
        s_diag.stage = BSP_QSPI_STAGE_HAL;
        s_hal_attempted = true;
        status = HAL_QSPI_Init(&s_qspi);
        s_diag.hal_status = status;
        result = Qspi_Result(status);
        if (result == SYS_OK && (s_diag.msp_called != 1U || s_diag.msp_result != SYS_OK ||
            s_qspi.State != HAL_QSPI_STATE_READY || !Qspi_ConfigValid()))
            result = SYS_ERR_IO;
        if (result == SYS_OK)
            result = Qspi_ReadId(started);
        s_diag.stage = result == SYS_OK ? BSP_QSPI_STAGE_VERIFY : s_diag.stage;
        if (result == SYS_OK && (!Qspi_ContextValid() || !Qspi_ConfigValid() ||
            s_qspi.State != HAL_QSPI_STATE_READY || s_qspi.ErrorCode != HAL_QSPI_ERROR_NONE))
            result = SYS_ERR_IO;
    }
    Qspi_Snapshot();
    s_diag.elapsed_ms = HAL_GetTick() - started;
    if (result == SYS_OK && s_diag.elapsed_ms >= QSPI_INIT_BUDGET_MS)
        result = SYS_ERR_TIMEOUT;
    s_diag.result = result;
    if (result == SYS_OK)
    {
        s_diag.ready = 1U;
        s_diag.stage = BSP_QSPI_STAGE_READY;
    }
    else
    {
        //HAL 已参与后设备状态不确定，锁存故障；DeInit 不提供隐式恢复通道。
        Qspi_RecordFault(!s_hal_attempted ? BSP_QSPI_FAULT_PREPARE :
            (s_diag.stage == BSP_QSPI_STAGE_HAL ? BSP_QSPI_FAULT_HAL_INIT :
             BSP_QSPI_FAULT_INIT_IDENTIFY), s_diag.stage, result, 0U,
            s_diag.stage == BSP_QSPI_STAGE_COMMAND || s_diag.stage == BSP_QSPI_STAGE_RECEIVE ?
            0x9FU : 0U, s_diag.hal_status);
        s_diag.fault = s_hal_attempted ? 1U : 0U;
        cleanup_started = HAL_GetTick();
        s_diag.cleanup_result = Qspi_Cleanup();
        s_diag.cleanup_elapsed_ms = HAL_GetTick() - cleanup_started;
        Qspi_RecordCleanup(s_diag.cleanup_result);
        if (s_diag.cleanup_result != SYS_OK)
            s_diag.fault = 1U;
    }
    Qspi_Give();
    return result;
}

SysResult_t BSP_QSPI_DeInit(void)
{
    SysResult_t result;
    uint32_t started;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    s_diag.ready = 0U;
    started = HAL_GetTick();
    result = Qspi_Cleanup();
    s_diag.cleanup_result = result;
    s_diag.cleanup_elapsed_ms = HAL_GetTick() - started;
    if (result != SYS_OK)
    {
        Qspi_RecordFault(BSP_QSPI_FAULT_CLEANUP, s_cleanup_stage, result, 0U, 0U,
            HAL_OK);
        s_diag.fault = 1U;
    }
    if (s_diag.fault != 0U)
        Qspi_RecordCleanup(result);
    Qspi_Give();
    return result;
}

SysResult_t BSP_QSPI_GetId(uint32_t *jedec_id)
{
    SysResult_t result = SYS_OK;
    if (jedec_id == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    if (s_diag.ready == 0U || s_diag.fault != 0U)
        result = SYS_ERR_INVALID_STATE;
    else
        *jedec_id = s_diag.jedec_id;
    Qspi_Give();
    return result;
}

SysResult_t BSP_QSPI_GetDiagnostics(BspQspiDiagnostics_t *diagnostics)
{
    if (diagnostics == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    *diagnostics = s_diag;
    Qspi_Give();
    return SYS_OK;
}

SysResult_t BSP_QSPI_GetFaultDiagnostics(BspQspiFaultDiagnostics_t *diagnostics)
{
    if (diagnostics == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    *diagnostics = s_fault;
    diagnostics->fault_latched = s_diag.fault;
    Qspi_Give();
    return SYS_OK;
}

//按256B提交有效前缀，HAL失败或短接收不污染调用者尚未确认的区间。
#define QSPI_READ_CHUNK_BYTES 256U
static uint8_t s_read_buffer[QSPI_READ_CHUNK_BYTES];
static bool s_read_fault;

static uint32_t Qspi_ReadRemaining(uint32_t started, uint32_t budget)
{
    uint32_t elapsed = HAL_GetTick() - started;
    if (elapsed >= budget)
        return 0U;
    return budget - elapsed > QSPI_IO_TIMEOUT_MS ? QSPI_IO_TIMEOUT_MS : budget - elapsed;
}

static bool Qspi_ReadHardwareValid(void)
{
    return Qspi_ContextValid() && Qspi_ConfigValid() &&
        s_qspi.State == HAL_QSPI_STATE_READY &&
        (QUADSPI->CCR & QUADSPI_CCR_FMODE_1) == 0U;
}

static SysResult_t Qspi_ReadTransfer(uint32_t opcode, uint32_t address,
    uint32_t address_mode, uint32_t data_mode, uint32_t dummy,
    uint32_t bytes, uint32_t started, uint32_t budget)
{
    QSPI_CommandTypeDef command = {0};
    HAL_StatusTypeDef hal;
    SysResult_t result;
    uint32_t remaining;
    s_diag.read_opcode = opcode;
    //每段和每条命令前复核硬件；短临界区只保护操作权，不跨 HAL 等待。
    if (!Qspi_ReadHardwareValid())
    {
        s_read_fault = true;
        return SYS_ERR_INVALID_STATE;
    }
    remaining = Qspi_ReadRemaining(started, budget);
    if (remaining == 0U)
    {
        s_read_fault = true;
        return SYS_ERR_TIMEOUT;
    }
    command.Instruction = opcode;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = address_mode;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.Address = address;
    command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    command.DataMode = data_mode;
#if defined(QSPI_QUAD_ID_DIAGNOSTIC)
    if (opcode == 0x94U)
    {
        //Figure 41：地址后发送 Fxh 模式字节，再进入空周期。
        command.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
        command.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
        command.AlternateBytes = 0xF0U;
    }
#endif
    command.DummyCycles = dummy;
    command.NbData = bytes;
    command.DdrMode = QSPI_DDR_MODE_DISABLE;
    command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    s_diag.read_stage = BSP_QSPI_READ_STAGE_COMMAND;
    hal = HAL_QSPI_Command(&s_qspi, &command, remaining);
    s_diag.read_hal_status = hal;
    result = Qspi_Result(hal);
    if (result == SYS_OK && QUADSPI->DLR != bytes - 1U)
        result = SYS_ERR_PROTOCOL;
    if (result == SYS_OK && !Qspi_ReadHardwareValid())
        result = SYS_ERR_INVALID_STATE;
    remaining = Qspi_ReadRemaining(started, budget);
    if (result == SYS_OK && remaining == 0U)
        result = SYS_ERR_TIMEOUT;
    if (result != SYS_OK)
    {
        s_read_fault = true;
        return result;
    }
    s_diag.read_stage = BSP_QSPI_READ_STAGE_RECEIVE;
    hal = HAL_QSPI_Receive(&s_qspi, s_read_buffer, remaining);
    s_diag.read_hal_status = hal;
    result = Qspi_Result(hal);
    if (result == SYS_OK && (s_qspi.RxXferSize != bytes || s_qspi.RxXferCount != 0U))
        result = SYS_ERR_PROTOCOL;
    if (result == SYS_OK && !Qspi_ReadHardwareValid())
        result = SYS_ERR_INVALID_STATE;
    if (result == SYS_OK && Qspi_ReadRemaining(started, budget) == 0U)
        result = SYS_ERR_TIMEOUT;
    if (result != SYS_OK)
        s_read_fault = true;
    return result;
}

static void Qspi_ReadBegin(uint32_t mode, uint32_t address, uint32_t size)
{
    s_diag.read_mode = mode;
    s_diag.read_address = address;
    s_diag.read_requested = size;
    s_diag.read_completed = 0U;
    s_diag.read_stage = BSP_QSPI_READ_STAGE_CHECK;
    s_diag.read_opcode = 0U;
    s_diag.read_hal_status = HAL_OK;
    s_diag.read_cleanup_result = SYS_OK;
    s_diag.read_cleanup_elapsed_ms = 0U;
    s_diag.read_cr = s_diag.read_dcr = s_diag.read_sr = 0U;
    s_diag.status_valid_mask = 0U;
    memset(&s_diag.status, 0, sizeof(s_diag.status));
    s_read_fault = false;
}

static SysResult_t Qspi_ReadFinish(SysResult_t result, uint32_t started)
{
    uint32_t cleanup_started;
    s_diag.read_result = result;
    s_diag.read_elapsed_ms = HAL_GetTick() - started;
    s_diag.read_hal_error = s_qspi.ErrorCode;
    s_diag.read_hal_state = s_qspi.State;
    if (s_owned && __HAL_RCC_QSPI_IS_CLK_ENABLED())
    {
        s_diag.read_cr = QUADSPI->CR;
        s_diag.read_dcr = QUADSPI->DCR;
        s_diag.read_sr = QUADSPI->SR;
    }
    if (s_read_fault)
    {
        //先保存首错再清理；异常 DMA 配置/上下文不能交给 HAL Abort 访问未知句柄。
        Qspi_RecordFault(BSP_QSPI_FAULT_READ, s_diag.read_stage, result,
            s_diag.read_address + s_diag.read_completed, s_diag.read_opcode, s_diag.read_hal_status);
        s_diag.ready = 0U;
        s_diag.fault = 1U;
        cleanup_started = HAL_GetTick();
        s_diag.read_cleanup_result = !Qspi_ContextValid() ||
            !__HAL_RCC_QSPI_IS_CLK_ENABLED() || (QUADSPI->CR & QUADSPI_CR_DMAEN) != 0U ?
            SYS_ERR_INVALID_STATE : Qspi_Cleanup();
        s_diag.read_cleanup_elapsed_ms = HAL_GetTick() - cleanup_started;
        Qspi_RecordCleanup(s_diag.read_cleanup_result);
    }
    else if (result == SYS_OK)
        s_diag.read_stage = BSP_QSPI_READ_STAGE_COMPLETE;
    Qspi_Give();
    return result;
}

static SysResult_t Qspi_ReadStatusLocked(uint32_t started, uint32_t budget)
{
    static const uint8_t commands[3] = {0x05U, 0x35U, 0x15U};
    uint32_t i;
    SysResult_t result;
    for (i = 0U; i < 3U; i++)
    {
        result = Qspi_ReadTransfer(commands[i], 0U, QSPI_ADDRESS_NONE,
            QSPI_DATA_1_LINE, 0U, 1U, started, budget);
        if (result != SYS_OK)
            return result;
        if (i == 0U)
        {
            s_diag.status.sr1 = s_read_buffer[0];
            s_diag.status.busy = s_read_buffer[0] & 1U;
            s_diag.status.wel = (s_read_buffer[0] >> 1) & 1U;
        }
        else if (i == 1U)
        {
            s_diag.status.sr2 = s_read_buffer[0];
            s_diag.status.qe = (s_read_buffer[0] >> 1) & 1U;
            s_diag.status.sus = (s_read_buffer[0] >> 7) & 1U;
        }
        else
            s_diag.status.sr3 = s_read_buffer[0];
        s_diag.status_valid_mask |= 1U << i;
    }
    return SYS_OK;
}

SysResult_t BSP_QSPI_ReadStatus(BspQspiStatus_t *status, uint32_t timeout_ms)
{
    uint32_t started;
    SysResult_t result;
    if (status == NULL || timeout_ms == 0U || timeout_ms > BSP_QSPI_MAX_READ_TIMEOUT_MS)
        return SYS_ERR_INVALID_ARG;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    if (s_diag.ready == 0U || s_diag.fault != 0U)
    {
        Qspi_Give();
        return SYS_ERR_INVALID_STATE;
    }
    started = HAL_GetTick();
    Qspi_ReadBegin(UINT32_MAX, 0U, 0U);
    result = Qspi_ReadStatusLocked(started, timeout_ms);
    if (result == SYS_OK)
        *status = s_diag.status;
    return Qspi_ReadFinish(result, started);
}

SysResult_t BSP_QSPI_Read(BspQspiReadMode_t mode, uint32_t address,
    void *buffer, uint32_t size, uint32_t *completed, uint32_t timeout_ms)
{
    uint32_t started, bytes;
    uintptr_t ptr = (uintptr_t)buffer, count_ptr = (uintptr_t)completed;
    SysResult_t result;
    //先用减法验证所有边界；此阶段不读取任何 QSPI/RCC/GPIO 寄存器。
    if (buffer == NULL || completed == NULL || size == 0U ||
        (uint32_t)mode > BSP_QSPI_READ_QUAD_OUTPUT ||
        address >= BSP_QSPI_CAPACITY_BYTES || size > BSP_QSPI_CAPACITY_BYTES - address ||
        timeout_ms == 0U || timeout_ms > BSP_QSPI_MAX_READ_TIMEOUT_MS ||
        ptr > UINT32_MAX - size || count_ptr > UINT32_MAX - sizeof(*completed) ||
        (count_ptr & 3U) != 0U ||
        (count_ptr < ptr + size && ptr < count_ptr + sizeof(*completed)))
        return SYS_ERR_INVALID_ARG;
    *completed = 0U;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    if (s_diag.ready == 0U || s_diag.fault != 0U)
    {
        Qspi_Give();
        return SYS_ERR_INVALID_STATE;
    }
    started = HAL_GetTick();
    Qspi_ReadBegin(mode, address, size);
    //在固定120MHz配置下拒绝03h；不为了兼容它更改共享时钟或运行时分频。
    if (mode == BSP_QSPI_READ_STANDARD && s_diag.serial_clock_hz > 50000000U)
        return Qspi_ReadFinish(SYS_ERR_NOT_SUPPORTED, started);
    result = Qspi_ReadStatusLocked(started, timeout_ms);
    if (result == SYS_OK && s_diag.status.busy != 0U)
        result = SYS_ERR_BUSY;
    if (result == SYS_OK && s_diag.status.sus != 0U)
        result = SYS_ERR_INVALID_STATE;
    //IQ 型号的 QE 应为1；异常只诊断并拒绝，不尝试写寄存器修复。
    if (result == SYS_OK && mode == BSP_QSPI_READ_QUAD_OUTPUT && s_diag.status.qe == 0U)
        result = SYS_ERR_NOT_SUPPORTED;
    while (result == SYS_OK && s_diag.read_completed < size)
    {
        bytes = size - s_diag.read_completed;
        if (bytes > QSPI_READ_CHUNK_BYTES)
            bytes = QSPI_READ_CHUNK_BYTES;
        result = Qspi_ReadTransfer(mode == BSP_QSPI_READ_STANDARD ? 0x03U :
            (mode == BSP_QSPI_READ_FAST ? 0x0BU : 0x6BU),
            address + s_diag.read_completed, QSPI_ADDRESS_1_LINE,
            mode == BSP_QSPI_READ_QUAD_OUTPUT ? QSPI_DATA_4_LINES : QSPI_DATA_1_LINE,
            mode == BSP_QSPI_READ_STANDARD ? 0U : 8U, bytes, started, timeout_ms);
        if (result != SYS_OK)
            break;
        s_diag.read_stage = BSP_QSPI_READ_STAGE_COPY;
        memcpy((uint8_t *)buffer + s_diag.read_completed, s_read_buffer, bytes);
        s_diag.read_completed += bytes;
        *completed = s_diag.read_completed;
        if (Qspi_ReadRemaining(started, timeout_ms) == 0U)
        {
            s_read_fault = true;
            result = SYS_ERR_TIMEOUT;
        }
    }
    return Qspi_ReadFinish(result, started);
}

#if defined(QSPI_SFDP_DIAGNOSTIC)
SysResult_t QspiDiagnostic_ReadSfdp(uint8_t data[QSPI_SFDP_BYTES])
{
    const uint32_t budget = 100U;
    uint32_t started;
    SysResult_t result;
    if (data == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    if (s_diag.ready == 0U || s_diag.fault != 0U ||
        s_diag.serial_clock_hz == 0U || s_diag.serial_clock_hz > QSPI_TARGET_HZ)
    {
        Qspi_Give();
        return SYS_ERR_INVALID_STATE;
    }
    started = HAL_GetTick();
    Qspi_ReadBegin(UINT32_MAX, 0U, QSPI_SFDP_BYTES);
    result = Qspi_ReadTransfer(0x5AU, 0U, QSPI_ADDRESS_1_LINE,
        QSPI_DATA_1_LINE, 8U, QSPI_SFDP_BYTES, started, budget);
    if (result == SYS_OK)
    {
        memcpy(data, s_read_buffer, QSPI_SFDP_BYTES);
        s_diag.read_completed = QSPI_SFDP_BYTES;
    }
    return Qspi_ReadFinish(result, started);
}
#endif

#if defined(QSPI_QUAD_ID_DIAGNOSTIC)
SysResult_t QspiDiagnostic_ReadQuadId(uint8_t id[QSPI_QUAD_ID_BYTES])
{
    const uint32_t budget = 100U;
    uint32_t started;
    SysResult_t result;
    if (id == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Qspi_ContextValid())
        return SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return SYS_ERR_BUSY;
    if (s_diag.ready == 0U || s_diag.fault != 0U ||
        s_diag.serial_clock_hz != QSPI_TARGET_HZ)
    {
        Qspi_Give();
        return SYS_ERR_INVALID_STATE;
    }
    started = HAL_GetTick();
    Qspi_ReadBegin(UINT32_MAX, 0U, QSPI_QUAD_ID_BYTES);
    result = Qspi_ReadStatusLocked(started, budget);
    if (result == SYS_OK && s_diag.status.busy != 0U)
        result = SYS_ERR_BUSY;
    if (result == SYS_OK && s_diag.status.sus != 0U)
        result = SYS_ERR_INVALID_STATE;
    if (result == SYS_OK && s_diag.status.qe == 0U)
        result = SYS_ERR_NOT_SUPPORTED;
    if (result == SYS_OK)
        result = Qspi_ReadTransfer(0x94U, 0U, QSPI_ADDRESS_4_LINES,
            QSPI_DATA_4_LINES, 4U, QSPI_QUAD_ID_BYTES, started, budget);
    if (result == SYS_OK)
    {
        memcpy(id, s_read_buffer, QSPI_QUAD_ID_BYTES);
        s_diag.read_completed = QSPI_QUAD_ID_BYTES;
    }
    return Qspi_ReadFinish(result, started);
}
#endif

#define QSPI_PROGRAM_BUSY_LIMIT_MS 20U
#define QSPI_SECTOR_BUSY_LIMIT_MS 500U

typedef struct
{
    BspQspiWriteDiagnostics_t *diag;
    BspQspiDiagnostics_t saved_read;
    uint32_t started, budget;
    bool armed, pending, fault, wren_attempted, mutation_attempted, flash_idle;
} QspiWrite_t;

static SysResult_t Qspi_WriteOpen(QspiWrite_t *write, const QspiWritePolicy_t *policy,
    uint32_t opcode, uint32_t address, const void *input, uint32_t size, uint32_t budget,
    BspQspiWriteDiagnostics_t *diag)
{
    uintptr_t src = (uintptr_t)input, dst = (uintptr_t)diag;
    if (diag == NULL || (dst & 3U) != 0U || dst > UINTPTR_MAX - sizeof(*diag))
        return SYS_ERR_INVALID_ARG;
    if (input != NULL && (src > UINTPTR_MAX - size ||
        (src < dst + sizeof(*diag) && dst < src + size)))
        return SYS_ERR_INVALID_ARG;
    memset(diag, 0, sizeof(*diag));
    diag->opcode = opcode;
    diag->address = address;
    diag->requested = size;
    diag->failed_address = address;
    if (input == NULL || address >= BSP_QSPI_CAPACITY_BYTES || size == 0U ||
        size > BSP_QSPI_CAPACITY_BYTES - address || budget == 0U ||
        budget > BSP_QSPI_MAX_WRITE_TIMEOUT_MS ||
        (opcode == 0x20U && (size != BSP_QSPI_SECTOR_BYTES ||
         (address % BSP_QSPI_SECTOR_BYTES) != 0U)))
        return diag->result = SYS_ERR_INVALID_ARG;
    if (policy->address >= BSP_QSPI_CAPACITY_BYTES ||
        policy->bytes > BSP_QSPI_CAPACITY_BYTES - policy->address ||
        address < policy->address || address - policy->address >= policy->bytes ||
        size > policy->bytes - (address - policy->address))
        return diag->result = SYS_ERR_NOT_SUPPORTED;
    if (!Qspi_ContextValid())
        return diag->result = SYS_ERR_INVALID_STATE;
    if (!Qspi_Take())
        return diag->result = SYS_ERR_BUSY;
    if (s_diag.ready == 0U || s_diag.fault != 0U)
    {
        diag->fault = s_diag.fault;
        Qspi_Give();
        return diag->result = SYS_ERR_INVALID_STATE;
    }
    write->diag = diag;
    write->saved_read = s_diag;
    write->started = HAL_GetTick();
    write->budget = budget;
    write->armed = write->pending = write->fault = false;
    write->wren_attempted = write->mutation_attempted = write->flash_idle = false;
    s_diag.read_hal_status = HAL_OK;
    s_read_fault = false;
    return SYS_OK;
}

static SysResult_t Qspi_WriteFinish(QspiWrite_t *write, SysResult_t result)
{
    BspQspiWriteDiagnostics_t *diag = write->diag;
    uint32_t cleanup_started;
    diag->result = result;
    diag->elapsed_ms = HAL_GetTick() - write->started;
    diag->hal_error = s_qspi.ErrorCode;
    diag->hal_state = s_qspi.State;
    if (s_owned && __HAL_RCC_QSPI_IS_CLK_ENABLED())
    {
        diag->cr = QUADSPI->CR;
        diag->dcr = QUADSPI->DCR;
        diag->sr = QUADSPI->SR;
    }
    s_diag = write->saved_read;
    if (diag->completed != 0U)
    {
        s_fault.confirmed_write_address = diag->address;
        s_fault.confirmed_write_bytes = diag->completed;
        s_fault.confirmed_write_opcode = diag->opcode;
    }
    if (result != SYS_OK)
    {
        BspQspiFaultOrigin_t origin = BSP_QSPI_FAULT_WRITE_PREFLIGHT;
        if (diag->stage == BSP_QSPI_WRITE_VERIFY)
            origin = BSP_QSPI_FAULT_WRITE_VERIFY;
        else if (write->pending)
            origin = diag->stage == BSP_QSPI_WRITE_BUSY ?
                BSP_QSPI_FAULT_FLASH_PENDING : BSP_QSPI_FAULT_MUTATION_COMMAND;
        else if (write->wren_attempted)
            origin = BSP_QSPI_FAULT_WREN;
        Qspi_RecordFault(origin, diag->stage, result, diag->failed_address,
            diag->wire_opcode, diag->hal_status);
        s_fault.flash_completion_unknown = write->pending;
        s_fault.wren_possible = write->wren_attempted;
        s_fault.mutation_possible = write->mutation_attempted;
        s_fault.flash_idle_verified = write->flash_idle;
    }
    if (write->fault || write->armed || write->pending || s_read_fault)
    {
        diag->fault = s_diag.fault = 1U;
        s_diag.ready = 0U;
        diag->flash_completion_unknown = write->pending;
        cleanup_started = HAL_GetTick();
        //Abort 只清理 MCU 事务。Flash 内部操作可能仍未完成。
        diag->cleanup_result = !Qspi_ContextValid() || !__HAL_RCC_QSPI_IS_CLK_ENABLED() ||
            (QUADSPI->CR & QUADSPI_CR_DMAEN) != 0U ? SYS_ERR_INVALID_STATE : Qspi_Cleanup();
        diag->cleanup_elapsed_ms = HAL_GetTick() - cleanup_started;
        Qspi_RecordCleanup(diag->cleanup_result);
    }
    if (result == SYS_OK)
    {
        diag->stage = BSP_QSPI_WRITE_COMPLETE;
        diag->failed_address = UINT32_MAX;
    }
    Qspi_Give();
    return result;
}

static SysResult_t Qspi_WriteRead(QspiWrite_t *write, uint32_t opcode, uint32_t address,
    uint32_t bytes, uint32_t budget)
{
    SysResult_t result;
    write->diag->wire_opcode = opcode;
    result = Qspi_ReadTransfer(opcode, address, opcode == 0x0BU ? QSPI_ADDRESS_1_LINE :
        QSPI_ADDRESS_NONE, QSPI_DATA_1_LINE, opcode == 0x0BU ? 8U : 0U,
        bytes, write->started, budget);
    write->diag->hal_status = s_diag.read_hal_status;
    return result;
}

static SysResult_t Qspi_WriteStatus(QspiWrite_t *write, bool all, uint32_t budget)
{
    BspQspiWriteDiagnostics_t *diag = write->diag;
    SysResult_t result;
    diag->status_valid_mask = 0U;
    result = Qspi_WriteRead(write, 0x05U, 0U, 1U, budget);
    if (result != SYS_OK)
        return result;
    diag->sr1 = s_read_buffer[0];
    diag->status_valid_mask = 1U;
    if (!all)
        return SYS_OK;
    result = Qspi_WriteRead(write, 0x35U, 0U, 1U, budget);
    if (result != SYS_OK)
        return result;
    diag->sr2 = s_read_buffer[0];
    diag->status_valid_mask |= 2U;
    result = Qspi_WriteRead(write, 0x15U, 0U, 1U, budget);
    if (result == SYS_OK)
    {
        diag->sr3 = s_read_buffer[0];
        diag->status_valid_mask |= 4U;
    }
    return result;
}

static bool Qspi_WriteProtected(const BspQspiWriteDiagnostics_t *diag)
{
    //保守拒绝任何 BP/CMP 或个别块锁模式，不推算可省略的保护区域。
    return (diag->sr1 & 0x1CU) != 0U || (diag->sr2 & 0x40U) != 0U ||
        (diag->sr3 & 0x04U) != 0U;
}

static SysResult_t Qspi_WriteIdle(QspiWrite_t *write, uint32_t budget)
{
    BspQspiWriteDiagnostics_t *diag = write->diag;
    SysResult_t result = Qspi_WriteStatus(write, true, budget);
    if (result != SYS_OK)
        return result;
    if ((diag->sr1 & 1U) != 0U)
        return SYS_ERR_BUSY;
    if ((diag->sr1 & 2U) != 0U || (diag->sr2 & 0x80U) != 0U)
        return SYS_ERR_INVALID_STATE;
    return Qspi_WriteProtected(diag) ? SYS_ERR_NOT_SUPPORTED : SYS_OK;
}

static SysResult_t Qspi_WriteCheckData(QspiWrite_t *write, uint32_t address, uint32_t size,
    const uint8_t *expected, bool publish_progress)
{
    uint32_t offset = 0U, bytes, i;
    SysResult_t result;
    while (offset < size)
    {
        bytes = size - offset;
        if (bytes > QSPI_READ_CHUNK_BYTES)
            bytes = QSPI_READ_CHUNK_BYTES;
        write->diag->failed_address = address + offset;
        result = Qspi_WriteRead(write, 0x0BU, address + offset, bytes, write->budget);
        if (result != SYS_OK)
            return result;
        for (i = 0U; i < bytes; i++)
        {
            if (s_read_buffer[i] != (expected == NULL ? 0xFFU : expected[offset + i]))
            {
                write->diag->failed_address += i;
                return SYS_ERR_PROTOCOL;
            }
        }
        if (Qspi_ReadRemaining(write->started, write->budget) == 0U)
            return SYS_ERR_TIMEOUT;
        if (publish_progress)
            write->diag->completed += bytes;
        offset += bytes;
    }
    return SYS_OK;
}

static SysResult_t Qspi_WriteCommand(QspiWrite_t *write, uint32_t opcode, uint32_t address,
    uint32_t size, uint32_t budget)
{
    QSPI_CommandTypeDef command = {0};
    SysResult_t result;
    uint32_t remaining;
    write->diag->wire_opcode = opcode;
    if (!Qspi_ReadHardwareValid())
    {
        write->fault = true;
        return SYS_ERR_INVALID_STATE;
    }
    remaining = Qspi_ReadRemaining(write->started, budget);
    if (remaining == 0U)
        return SYS_ERR_TIMEOUT;
    command.Instruction = opcode;
    command.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    command.AddressMode = opcode == 0x06U ? QSPI_ADDRESS_NONE : QSPI_ADDRESS_1_LINE;
    command.AddressSize = QSPI_ADDRESS_24_BITS;
    command.Address = address;
    command.DataMode = size == 0U ? QSPI_DATA_NONE : QSPI_DATA_1_LINE;
    command.NbData = size;
    command.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    if (opcode == 0x06U)
    {
        write->armed = true;
        write->wren_attempted = true;
        write->flash_idle = false;
    }
    else
    {
        write->pending = true;
        write->mutation_attempted = true;
    }
    write->diag->hal_status = HAL_QSPI_Command(&s_qspi, &command, remaining);
    result = Qspi_Result((HAL_StatusTypeDef)write->diag->hal_status);
    if (result == SYS_OK && !Qspi_ReadHardwareValid())
        return SYS_ERR_INVALID_STATE;
    if (result == SYS_OK && size != 0U && QUADSPI->DLR != size - 1U)
        return SYS_ERR_PROTOCOL;
    return result;
}

static SysResult_t Qspi_WriteEnable(QspiWrite_t *write, uint32_t address, uint32_t budget)
{
    BspQspiWriteDiagnostics_t *diag = write->diag;
    SysResult_t result;
    diag->failed_address = address;
    diag->stage = BSP_QSPI_WRITE_STATUS;
    result = Qspi_WriteIdle(write, budget);
    if (result != SYS_OK)
        return result;
    diag->stage = BSP_QSPI_WRITE_ENABLE;
    result = Qspi_WriteCommand(write, 0x06U, 0U, 0U, budget);
    if (result != SYS_OK)
        return result;
    diag->stage = BSP_QSPI_WRITE_WEL;
    result = Qspi_WriteStatus(write, false, budget);
    if (result != SYS_OK)
        return result;
    if ((diag->sr1 & 1U) != 0U)
        return SYS_ERR_BUSY;
    if ((diag->sr1 & 2U) == 0U)
    {
        write->armed = false;
        return SYS_ERR_PROTOCOL;
    }
    return SYS_OK;
}

static SysResult_t Qspi_WriteWait(QspiWrite_t *write, uint32_t maximum_ms)
{
    BspQspiWriteDiagnostics_t *diag = write->diag;
    uint32_t budget = HAL_GetTick() - write->started + maximum_ms;
    SysResult_t result;
    if (budget > write->budget)
        budget = write->budget;
    diag->stage = BSP_QSPI_WRITE_BUSY;
    do
    {
        result = Qspi_WriteStatus(write, false, budget);
        if (result != SYS_OK)
            return result;
    } while ((diag->sr1 & 1U) != 0U);
    result = Qspi_WriteIdle(write, budget);
    if (result == SYS_OK)
    {
        write->pending = write->armed = false;
        write->flash_idle = true;
    }
    return result;
}

static SysResult_t Qspi_ProgramPage(QspiWrite_t *write, uint32_t address,
    const uint8_t *data, uint32_t bytes)
{
    uint32_t budget, remaining;
    SysResult_t result;
    if (write->budget <= QSPI_PROGRAM_BUSY_LIMIT_MS)
        return SYS_ERR_TIMEOUT;
    budget = write->budget - QSPI_PROGRAM_BUSY_LIMIT_MS;
    result = Qspi_WriteEnable(write, address, budget);
    if (result != SYS_OK)
        return result;
    write->diag->stage = BSP_QSPI_WRITE_COMMAND;
    result = Qspi_WriteCommand(write, 0x02U, address, bytes, budget);
    if (result != SYS_OK)
        return result;
    remaining = Qspi_ReadRemaining(write->started, budget);
    if (remaining == 0U)
        return SYS_ERR_TIMEOUT;
    write->diag->stage = BSP_QSPI_WRITE_TRANSMIT;
    write->diag->hal_status = HAL_QSPI_Transmit(&s_qspi, (uint8_t *)data, remaining);
    result = Qspi_Result((HAL_StatusTypeDef)write->diag->hal_status);
    if (result == SYS_OK && (s_qspi.TxXferSize != bytes || s_qspi.TxXferCount != 0U))
        result = SYS_ERR_PROTOCOL;
    if (result == SYS_OK && !Qspi_ReadHardwareValid())
        result = SYS_ERR_INVALID_STATE;
    return result == SYS_OK ? Qspi_WriteWait(write, QSPI_PROGRAM_BUSY_LIMIT_MS) : result;
}

static SysResult_t Qspi_ProgramWithPolicy(const QspiWritePolicy_t *policy, uint32_t address,
    const void *buffer, uint32_t size, uint32_t timeout_ms, BspQspiWriteDiagnostics_t *diag)
{
    QspiWrite_t write;
    const uint8_t *data = (const uint8_t *)buffer;
    uint32_t offset = 0U, bytes;
    SysResult_t result = Qspi_WriteOpen(&write, policy, 0x02U, address, buffer, size,
        timeout_ms, diag);
    if (result != SYS_OK)
        return result;
    diag->stage = BSP_QSPI_WRITE_STATUS;
    result = Qspi_WriteIdle(&write, write.budget);
    if (result == SYS_OK)
    {
        diag->stage = BSP_QSPI_WRITE_PREFLIGHT;
        result = Qspi_WriteCheckData(&write, address, size, NULL, false);
    }
    //完整地址范围和目标内容通过后才允许第一页写入。
    while (result == SYS_OK && offset < size)
    {
        diag->failed_address = address + offset;
        bytes = BSP_QSPI_PAGE_BYTES - ((address + offset) % BSP_QSPI_PAGE_BYTES);
        if (bytes > size - offset)
            bytes = size - offset;
        result = Qspi_ProgramPage(&write, address + offset, data + offset, bytes);
        if (result == SYS_OK)
        {
            diag->stage = BSP_QSPI_WRITE_VERIFY;
            result = Qspi_WriteCheckData(&write, address + offset, bytes, data + offset, true);
            if (result != SYS_OK)
                write.fault = true;
        }
        offset += bytes;
    }
    return Qspi_WriteFinish(&write, result);
}

SysResult_t BSP_QSPI_Program(uint32_t address, const void *buffer, uint32_t size,
    uint32_t timeout_ms, BspQspiWriteDiagnostics_t *diagnostics)
{
    return Qspi_ProgramWithPolicy(&s_write_policy, address, buffer, size, timeout_ms, diagnostics);
}

static SysResult_t Qspi_EraseWithPolicy(const QspiWritePolicy_t *policy, uint32_t address,
    const void *backup, uint32_t backup_size, uint32_t timeout_ms,
    BspQspiWriteDiagnostics_t *diag)
{
    QspiWrite_t write;
    uint32_t command_budget;
    SysResult_t result = Qspi_WriteOpen(&write, policy, 0x20U, address, backup, backup_size,
        timeout_ms, diag);
    if (result != SYS_OK)
        return result;
    if (write.budget <= QSPI_SECTOR_BUSY_LIMIT_MS)
        return Qspi_WriteFinish(&write, SYS_ERR_TIMEOUT);
    command_budget = write.budget - QSPI_SECTOR_BUSY_LIMIT_MS;
    diag->stage = BSP_QSPI_WRITE_STATUS;
    result = Qspi_WriteIdle(&write, command_budget);
    if (result == SYS_OK)
    {
        diag->stage = BSP_QSPI_WRITE_PREFLIGHT;
        //逐字节核对整个扇区，备份不一致时禁止发送 WREN。
        result = Qspi_WriteCheckData(&write, address, BSP_QSPI_SECTOR_BYTES, backup, false);
    }
    if (result == SYS_OK)
        result = Qspi_WriteEnable(&write, address, command_budget);
    if (result == SYS_OK)
    {
        diag->stage = BSP_QSPI_WRITE_COMMAND;
        result = Qspi_WriteCommand(&write, 0x20U, address, 0U, command_budget);
    }
    if (result == SYS_OK)
        result = Qspi_WriteWait(&write, QSPI_SECTOR_BUSY_LIMIT_MS);
    if (result == SYS_OK)
    {
        diag->stage = BSP_QSPI_WRITE_VERIFY;
        result = Qspi_WriteCheckData(&write, address, BSP_QSPI_SECTOR_BYTES, NULL, true);
        if (result != SYS_OK)
            write.fault = true;
    }
    return Qspi_WriteFinish(&write, result);
}

SysResult_t BSP_QSPI_EraseSector(uint32_t address, const void *backup, uint32_t backup_size,
    uint32_t timeout_ms, BspQspiWriteDiagnostics_t *diagnostics)
{
    return Qspi_EraseWithPolicy(&s_write_policy, address, backup, backup_size,
        timeout_ms, diagnostics);
}

#if defined(QSPI_H1_BACKUP) || defined(QSPI_H1_BACKUP120) || defined(QSPI_H1_WRITE)
#include "../../Tests/QspiFlashQ1/hardware/qspi_h1.inc"
#endif
