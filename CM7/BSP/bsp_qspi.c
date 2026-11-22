#include "bsp_qspi.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define QSPI_TARGET_HZ 120000000UL
//首次 HAL Init 强制使用 5000ms；预算另留准备/识别余量，不能虚报为短超时。
#define QSPI_INIT_BUDGET_MS (HAL_QSPI_TIMEOUT_DEFAULT_VALUE + 200U)
#define QSPI_IO_TIMEOUT_MS 100U
#define QSPI_F_PINS (GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10)
#define QSPI_G_PINS GPIO_PIN_6
#define QSPI_F_MASK 0x003FF000UL
#define QSPI_G_MASK 0x00003000UL

static QSPI_HandleTypeDef s_qspi;
static BspQspiDiagnostics_t s_diag;
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
        s_diag.fault = s_hal_attempted ? 1U : 0U;
        cleanup_started = HAL_GetTick();
        s_diag.cleanup_result = Qspi_Cleanup();
        s_diag.cleanup_elapsed_ms = HAL_GetTick() - cleanup_started;
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
        s_diag.fault = 1U;
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
        s_diag.ready = 0U;
        s_diag.fault = 1U;
        cleanup_started = HAL_GetTick();
        s_diag.read_cleanup_result = !Qspi_ContextValid() ||
            !__HAL_RCC_QSPI_IS_CLK_ENABLED() || (QUADSPI->CR & QUADSPI_CR_DMAEN) != 0U ?
            SYS_ERR_INVALID_STATE : Qspi_Cleanup();
        s_diag.read_cleanup_elapsed_ms = HAL_GetTick() - cleanup_started;
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
