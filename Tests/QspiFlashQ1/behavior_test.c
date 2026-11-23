//软件测试直接编译生产 BSP；真实 CMSIS 寄存器布局运行在 ARM 仿真中，不连接板卡。
#include "stm32h7xx_hal.h"
static uint32_t s_test_ipsr;
//异常上下文由测试注入；其余 CMSIS 屏蔽寄存器仍执行真实 ARM 指令。
#define __get_IPSR() s_test_ipsr
#include "../../CM7/BSP/bsp_qspi.c"

uint32_t g_assertions, g_failure, g_commands, g_init_calls;
static uint32_t s_case, s_tick, s_hclk, s_timeout_seen, s_command_timeout, s_receive_timeout;
static SysResult_t s_nested;
//Q1.2 复用原有初始化夹具；空钩子保持全部 Q1.1 用例原样执行。
static HAL_StatusTypeDef (*s_read_command_hook)(QSPI_HandleTypeDef *, QSPI_CommandTypeDef *, uint32_t);
static HAL_StatusTypeDef (*s_read_receive_hook)(QSPI_HandleTypeDef *, uint8_t *, uint32_t);
static HAL_StatusTypeDef (*s_read_abort_hook)(QSPI_HandleTypeDef *);
static uint32_t (*s_tick_hook)(void);
static HAL_StatusTypeDef (*s_write_transmit_hook)(QSPI_HandleTypeDef *, uint8_t *, uint32_t);
#define CHECK(x) do { g_assertions++; if (!(x) && g_failure == 0U) g_failure = __LINE__; } while (0)

uint32_t HAL_GetTick(void)
{
    if (s_tick_hook != NULL)
        return s_tick_hook();
    return s_tick++;
}

HAL_StatusTypeDef HAL_QSPI_Transmit(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    if (s_write_transmit_hook != NULL)
        return s_write_transmit_hook(handle, data, timeout);
    CHECK(false);
    return HAL_ERROR;
}

uint32_t HAL_RCC_GetHCLKFreq(void)
{
    return s_hclk;
}

void HAL_GPIO_Init(GPIO_TypeDef *gpio, const GPIO_InitTypeDef *init)
{
    uint32_t pin;
    CHECK(__get_PRIMASK() == 0U);
    if (s_case == 3U)
        return;
    for (pin = 0U; pin < 16U; pin++)
    {
        if ((init->Pin & (1U << pin)) == 0U)
            continue;
        MODIFY_REG(gpio->MODER, 3U << (pin * 2U), 2U << (pin * 2U));
        CLEAR_BIT(gpio->OTYPER, 1U << pin);
        MODIFY_REG(gpio->OSPEEDR, 3U << (pin * 2U), init->Speed << (pin * 2U));
        MODIFY_REG(gpio->PUPDR, 3U << (pin * 2U), init->Pull << (pin * 2U));
        MODIFY_REG(gpio->AFR[pin / 8U], 15U << ((pin % 8U) * 4U),
            init->Alternate << ((pin % 8U) * 4U));
    }
    if (s_case == 45U)
        __HAL_RCC_QSPI_CONFIG(RCC_QSPICLKSOURCE_PLL);
    if (s_case == 46U)
        __HAL_RCC_QSPI_CLK_DISABLE();
    if (s_case == 47U)
        gpio->OSPEEDR = 0U;
}

HAL_StatusTypeDef HAL_QSPI_Init(QSPI_HandleTypeDef *handle)
{
    uint32_t saved;
    g_init_calls++;
    CHECK(__get_PRIMASK() == 0U);
    CHECK(handle->Instance == QUADSPI && handle->State == HAL_QSPI_STATE_RESET);
    CHECK(handle->Init.FlashSize == 23U && handle->Init.FifoThreshold == 1U);
    if (s_case == 4U)
    {
        //故意跳过 MSP，但仍然返回 HAL_OK。
    }
    else if (s_case == 5U)
    {
        saved = GPIOG->AFR[0];
        GPIOG->AFR[0] = 0U;
        HAL_QSPI_MspInit(handle);
        GPIOG->AFR[0] = saved;
    }
    else
        HAL_QSPI_MspInit(handle);
    handle->Timeout = HAL_QSPI_TIMEOUT_DEFAULT_VALUE;
    s_timeout_seen = handle->Timeout;
    if (s_case == 6U)
    {
        handle->ErrorCode = HAL_QSPI_ERROR_TRANSFER;
        return HAL_ERROR;
    }
    if (s_case == 31U)
    {
        s_tick += handle->Timeout + 1U;
        handle->ErrorCode = HAL_QSPI_ERROR_TIMEOUT;
        return HAL_TIMEOUT;
    }
    if (s_case == 44U)
    {
        handle->ErrorCode = HAL_QSPI_ERROR_TIMEOUT;
        return HAL_ERROR;
    }
    if (s_case == 32U)
        s_tick += 5300U;
    if (s_case == 30U)
        s_tick += 5150U;
    QUADSPI->CR = (handle->Init.ClockPrescaler << QUADSPI_CR_PRESCALER_Pos) |
        handle->Init.SampleShifting | QUADSPI_CR_EN;
    QUADSPI->DCR = (23U << QUADSPI_DCR_FSIZE_Pos) | QSPI_CS_HIGH_TIME_8_CYCLE;
    handle->State = HAL_QSPI_STATE_READY;
    if (s_case == 7U)
        QUADSPI->DCR ^= 1U << QUADSPI_DCR_FSIZE_Pos;
    if (s_case == 26U)
        s_nested = BSP_QSPI_Init();
    if (s_case == 41U)
    {
        BspQspiDiagnostics_t diag;
        uint32_t value;
        CHECK(BSP_QSPI_DeInit() == SYS_ERR_BUSY);
        CHECK(BSP_QSPI_GetId(&value) == SYS_ERR_BUSY);
        CHECK(BSP_QSPI_GetDiagnostics(&diag) == SYS_ERR_BUSY);
    }
    if (s_case == 39U)
        QUADSPI->SR = QUADSPI_SR_BUSY;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_QSPI_Command(QSPI_HandleTypeDef *handle, QSPI_CommandTypeDef *cmd, uint32_t timeout)
{
    if (s_read_command_hook != NULL)
        return s_read_command_hook(handle, cmd, timeout);
    (void)handle;
    g_commands++;
    CHECK(__get_PRIMASK() == 0U);
    //所有可达路径只准 9F，没有写入/擦除/解锁/复位指令。
    CHECK(cmd->Instruction == 0x9FU && cmd->InstructionMode == QSPI_INSTRUCTION_1_LINE);
    CHECK(cmd->AddressMode == QSPI_ADDRESS_NONE && cmd->AlternateByteMode == QSPI_ALTERNATE_BYTES_NONE);
    CHECK(cmd->DataMode == QSPI_DATA_1_LINE && cmd->NbData == 3U && cmd->DummyCycles == 0U);
    CHECK(cmd->DdrMode == QSPI_DDR_MODE_DISABLE && cmd->SIOOMode == QSPI_SIOO_INST_EVERY_CMD);
    CHECK(timeout > 0U && timeout <= 100U);
    s_command_timeout = timeout;
    QUADSPI->DLR = s_case == 50U ? 20U : cmd->NbData - 1U;
    if (s_case == 11U)
        return HAL_BUSY;
    if (s_case == 12U || s_case == 33U)
        return HAL_ERROR;
    if (s_case == 13U)
        return HAL_TIMEOUT;
    if (s_case == 30U)
        s_tick += 15U;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_QSPI_Receive(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    if (s_read_receive_hook != NULL)
        return s_read_receive_hook(handle, data, timeout);
    CHECK(__get_PRIMASK() == 0U);
    CHECK(timeout > 0U && timeout <= 100U);
    s_receive_timeout = timeout;
    handle->RxXferSize = 3U;
    handle->RxXferCount = 0U;
    data[0] = 0xEFU;
    data[1] = 0x40U;
    data[2] = 0x18U;
    if (s_case == 8U)
        data[0] = data[1] = data[2] = 0U;
    if (s_case == 9U)
        data[0] = data[1] = data[2] = 255U;
    if (s_case == 10U)
        data[2] = 0x17U;
    if (s_case == 14U)
        return HAL_BUSY;
    if (s_case == 15U)
        return HAL_ERROR;
    if (s_case == 16U)
        return HAL_TIMEOUT;
    if (s_case == 28U)
        handle->RxXferCount = 1U;
    if (s_case == 29U)
        handle->RxXferSize = 2U;
    if (s_case == 34U)
        s_tick += 5200U;
    if (s_case == 35U)
        GPIOF->AFR[1] = 0U;
    if (s_case == 36U)
        TIM6->DIER = 0U;
    if (s_case == 37U)
        handle->ErrorCode = HAL_QSPI_ERROR_TRANSFER;
    if (s_case == 49U)
        QUADSPI->CR |= QUADSPI_CR_DMAEN;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_QSPI_Abort(QSPI_HandleTypeDef *handle)
{
    if (s_read_abort_hook != NULL)
        return s_read_abort_hook(handle);
    CHECK(__get_PRIMASK() == 0U);
    CHECK(handle->Timeout == 100U);
    if (s_case == 33U)
        return HAL_ERROR;
    if (s_case == 42U)
        s_tick += 200U;
    QUADSPI->SR = 0U;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_QSPI_DeInit(QSPI_HandleTypeDef *handle)
{
    CHECK(__get_PRIMASK() == 0U);
    if (s_case == 43U)
        return HAL_ERROR;
    QUADSPI->CR = 0U;
    handle->State = HAL_QSPI_STATE_RESET;
    return HAL_OK;
}

int Q1_TestRun(uint32_t id)
{
    SysResult_t result;
    BspQspiDiagnostics_t diag;
    uint32_t cached = 0x12345678U, saved_f, saved_g, calls;
    memset(&s_qspi, 0, sizeof(s_qspi));
    memset(&s_diag, 0, sizeof(s_diag));
    s_owned = s_busy = s_hal_attempted = false;
    s_case = id;
    s_test_ipsr = id == 40U ? 16U : 0U;
    s_tick = id == 27U ? 0xFFFFFFF0U : 100U;
    s_hclk = id == 24U ? 0U : (id == 25U ? 239000001U : 240000000U);
    __set_PRIMASK(0U);
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 4U);
    GPIOF->MODER = 0xFFFFFFFFU;
    GPIOG->MODER = 0xFFFFFFFFU;
    GPIOF->AFR[0] = 0x76543210U;
    GPIOF->AFR[1] = 0x76543210U;
    GPIOG->AFR[0] = 0x76543210U;
    saved_f = GPIOF->MODER;
    saved_g = GPIOG->MODER;
    if (id == 17U)
        __set_PRIMASK(1U);
    if (id == 18U)
        __set_BASEPRI(0x40U);
    if (id == 19U)
        __set_FAULTMASK(1U);
    if (id == 20U)
        TIM6->CR1 = 0U;
    if (id == 21U)
        TIM6->DIER = 0U;
    if (id == 22U)
        NVIC->ISER[(uint32_t)TIM6_DAC_IRQn >> 5U] = 0U;
    if (id == 23U)
        s_busy = true;
    if (id == 48U)
        __set_BASEPRI(0x50U);
    if (id == 38U)
        __HAL_RCC_QSPI_CLK_ENABLE();
    result = BSP_QSPI_Init();
    s_test_ipsr = 0U;
    __set_PRIMASK(0U);
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    if (id == 23U)
        s_busy = false;
    CHECK(BSP_QSPI_GetDiagnostics(&diag) == SYS_OK);
    if (id == 0U || id == 1U || id == 2U || id == 25U || id == 26U || id == 27U || id == 30U || id == 41U || id == 42U || id == 43U || id == 48U)
    {
        CHECK(result == SYS_OK && diag.ready == 1U && diag.fault == 0U);
        CHECK(diag.msp_called == 1U && diag.msp_result == SYS_OK);
        CHECK(diag.received_bytes == 3U && diag.init_timeout_ms == 5000U && s_timeout_seen == 5000U);
        CHECK(diag.prescaler == 1U && diag.serial_clock_hz <= 120000000U);
        CHECK((diag.cr & QUADSPI_CR_SSHIFT) != 0U);
        CHECK((GPIOF->MODER & ~QSPI_F_MASK) == (saved_f & ~QSPI_F_MASK));
        CHECK((GPIOG->MODER & ~QSPI_G_MASK) == (saved_g & ~QSPI_G_MASK));
        CHECK(BSP_QSPI_GetId(&cached) == SYS_OK && cached == BSP_QSPI_EXPECTED_ID);
        CHECK(g_commands == 1U);
        if (id == 1U)
            CHECK(BSP_QSPI_Init() == SYS_ERR_ALREADY_EXISTS && g_init_calls == 1U);
        if (id == 26U)
            CHECK(s_nested == SYS_ERR_BUSY);
        if (id == 30U)
            CHECK(s_receive_timeout < s_command_timeout && s_command_timeout < 100U);
        if (id == 2U)
        {
            CHECK(BSP_QSPI_DeInit() == SYS_OK);
            CHECK(GPIOF->MODER == saved_f && GPIOG->MODER == saved_g);
            CHECK(GPIOF->AFR[0] == 0x76543210U && GPIOF->AFR[1] == 0x76543210U);
            CHECK(GPIOG->AFR[0] == 0x76543210U && !__HAL_RCC_QSPI_IS_CLK_ENABLED());
            CHECK(__HAL_RCC_GPIOF_IS_CLK_ENABLED() && __HAL_RCC_GPIOG_IS_CLK_ENABLED());
            CHECK(BSP_QSPI_Init() == SYS_OK);
        }
        if (id == 42U || id == 43U)
        {
            CHECK(BSP_QSPI_DeInit() != SYS_OK);
            CHECK(s_diag.ready == 0U && s_diag.fault == 1U && s_owned);
        }
    }
    else
    {
        CHECK(result != SYS_OK && diag.ready == 0U);
        CHECK(BSP_QSPI_GetId(&cached) == SYS_ERR_INVALID_STATE && cached == 0x12345678U);
        if (id == 3U || (id >= 17U && id <= 24U) || id == 38U || id == 40U || (id >= 45U && id <= 47U))
            CHECK(g_init_calls == 0U && g_commands == 0U);
        if (id == 4U || id == 5U || id == 6U || id == 7U || id == 31U || id == 32U || id == 39U)
            CHECK(g_commands == 0U);
        if (id == 13U || id == 16U || id == 31U || id == 32U || id == 34U || id == 44U)
            CHECK(result == SYS_ERR_TIMEOUT);
        if (id == 11U || id == 14U || id == 23U || id == 38U)
            CHECK(result == SYS_ERR_BUSY);
        if (id == 33U || id == 36U)
            CHECK(diag.cleanup_result != SYS_OK && diag.fault == 1U && s_owned);
        if (diag.fault != 0U)
        {
            calls = g_init_calls;
            CHECK(BSP_QSPI_Init() == SYS_ERR_INVALID_STATE && g_init_calls == calls);
            (void)BSP_QSPI_DeInit();
            CHECK(s_diag.fault == 1U && BSP_QSPI_Init() == SYS_ERR_INVALID_STATE);
        }
    }
    CHECK(BSP_QSPI_GetId(NULL) == SYS_ERR_INVALID_ARG);
    CHECK(BSP_QSPI_GetDiagnostics(NULL) == SYS_ERR_INVALID_ARG);
    return g_failure == 0U ? 0 : 1;
}
