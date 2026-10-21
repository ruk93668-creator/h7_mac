#include "main.h"
#include "sdram_test.h"

#define SDRAM_WORD_COUNT (SDRAM_CAPACITY_BYTES / sizeof(uint32_t))
#define SDRAM_HALFWORD_COUNT (SDRAM_CAPACITY_BYTES / sizeof(uint16_t))
#define SDRAM_REFRESH_50MHZ 370U
#define SDRAM_REFRESH_100MHZ 761U
#define SDRAM_REFRESH_110MHZ 839U
#define SDRAM_READ_EDGE_PINS (GPIO_PIN_6 | GPIO_PIN_11 | GPIO_PIN_15)
#define SDRAM_DQ_EDGE_PINS (GPIO_PIN_10 | GPIO_PIN_14)
#define SDRAM_DQ_D_PINS (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 | \
                         GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15)
#define SDRAM_DQ_E_PINS (GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | \
                         GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

static SDRAM_HandleTypeDef s_sdram;
static uint8_t s_fmc_clock_ok;
static uint32_t s_refresh_count;

volatile uint32_t g_sdram_init_ok;
volatile uint32_t g_sdram_test_done;
volatile uint32_t g_sdram_error_count;
volatile uint32_t g_sdram_fail_address;
volatile uint32_t g_sdram_expected_value;
volatile uint32_t g_sdram_actual_value;
volatile uint32_t g_sdram_test_phase;
volatile uint32_t g_sdram_test_progress_pct;
volatile uint32_t g_sdram_test_clock_mhz = 50U;
volatile uint32_t g_sdram_stress_started;
volatile uint32_t g_sdram_stress_round;
volatile uint32_t g_sdram_stress_pattern;
volatile uint32_t g_sdram_stress_completed_sweeps;
volatile uint32_t g_sdram_stress_round_pass_mask[SDRAM_STRESS_ROUNDS];
volatile uint32_t g_sdram_line_scan_done;
volatile uint32_t g_sdram_dq_data_fail_mask;
volatile uint32_t g_sdram_dq_bias_fail_mask;
volatile uint32_t g_sdram_dq_fail_mask;
volatile uint32_t g_sdram_dq_pass_count;
volatile uint32_t g_sdram_addr_tested_mask;
volatile uint32_t g_sdram_addr_fail_mask;
volatile uint32_t g_sdram_addr_pass_count;
volatile uint32_t g_sdram_addr_last_ok;
volatile uint32_t g_sdram_dqm_low_ok;
volatile uint32_t g_sdram_dqm_high_ok;
volatile uint32_t g_sdram_dqm_tested;
volatile uint32_t g_sdram_dqm_low_readback;
volatile uint32_t g_sdram_dqm_high_readback;
volatile uint32_t g_sdram_dq4_zero_first;
volatile uint32_t g_sdram_dq4_zero_repeat;
volatile uint32_t g_sdram_dq4_zero_pulldown;
volatile uint32_t g_sdram_dq4_zero_pullup;
volatile uint32_t g_sdram_dq4_zero_far;
volatile uint32_t g_sdram_dq4_ones;
volatile uint32_t g_sdram_diag_gpio_af_errors;
volatile uint32_t g_sdram_diag_sdcr1;
volatile uint32_t g_sdram_diag_sdcr2;
volatile uint32_t g_sdram_diag_sdtr1;
volatile uint32_t g_sdram_diag_sdtr2;
volatile uint32_t g_sdram_diag_sdsr;
volatile uint32_t g_sdram_diag_sdrtr;
volatile uint32_t g_sdram_diag_sdcmr;
volatile uint32_t g_sdram_probe_lane0[6];
volatile uint32_t g_sdram_probe_lane1[6];
volatile uint32_t g_sdram_diag_gpio_b_idr;
volatile uint32_t g_sdram_diag_gpio_h_idr;
volatile uint32_t g_sdram_diag_gpio_f_idr;
volatile uint32_t g_sdram_diag_gpio_g_idr;
volatile uint32_t g_sdram_diag_cke_after_fmc_init;
volatile uint32_t g_sdram_diag_cke_after_clk_enable;
volatile uint32_t g_sdram_diag_cke_after_pall;
volatile uint32_t g_sdram_diag_cke_after_auto_refresh;
volatile uint32_t g_sdram_diag_cke_after_load_mode;
volatile uint32_t g_sdram_diag_ph7_gpio_idr;
volatile uint32_t g_sdram_diag_ph7_moder_pre_clken;
volatile uint32_t g_sdram_diag_ph7_odr_pre_clken;
volatile uint32_t g_sdram_diag_ph7_idr_pre_clken;
volatile uint32_t g_sdram_diag_ph7_moder_post_clken;
volatile uint32_t g_sdram_diag_ph7_odr_post_clken;
volatile uint32_t g_sdram_diag_ph7_idr_post_clken;
volatile uint32_t g_sdram_diag_dq_pullup_errors;
volatile uint32_t g_sdram_diag_dq_d_idr;
volatile uint32_t g_sdram_diag_dq_e_idr;
volatile uint32_t g_sdram_diag_ctrl_high_mask;
volatile uint32_t g_sdram_diag_ctrl_low_mask;
volatile uint32_t g_sdram_diag_ctrl_af_errors;
volatile uint32_t g_sdram_diag_we_exti_port;
volatile uint32_t g_sdram_diag_we_exti_swtest;
volatile uint32_t g_sdram_diag_we_exti_imr;
volatile uint32_t g_sdram_diag_we_exti_selftest;
volatile uint32_t g_sdram_diag_we_exti_access;
volatile uint32_t g_sdram_diag_read_exti_swtest;
volatile uint32_t g_sdram_diag_read_exti_selftest;
volatile uint32_t g_sdram_diag_read_exti_idle;
volatile uint32_t g_sdram_diag_read_exti_access;
volatile uint32_t g_sdram_diag_read_value;
volatile uint32_t g_sdram_diag_read_exti_clear;
volatile uint32_t g_sdram_diag_read_refresh_off;
volatile uint32_t g_sdram_diag_read_refresh_restored;
volatile uint32_t g_sdram_diag_read_precharge_ok;
volatile uint32_t g_sdram_diag_bias_written;
volatile uint32_t g_sdram_diag_bias_read_up;
volatile uint32_t g_sdram_diag_bias_read_down;
volatile uint32_t g_sdram_diag_bias_read_restored;
volatile uint32_t g_sdram_diag_bias_down_gpio_errors;
volatile uint32_t g_sdram_diag_dq_edge_swtest;
volatile uint32_t g_sdram_diag_dq_edge_selftest;
volatile uint32_t g_sdram_diag_dq_edge_idle;
volatile uint32_t g_sdram_diag_dq_edge_write;
volatile uint32_t g_sdram_diag_dqm_forced_mode;
volatile uint32_t g_sdram_diag_dqm_forced_idr;
volatile uint32_t g_sdram_diag_dqm_read_up;
volatile uint32_t g_sdram_diag_dqm_read_down;
volatile uint32_t g_sdram_diag_dqm_restored_af_errors;

static uint32_t CheckFmcAfPins(GPIO_TypeDef *port, uint32_t pin_mask);

static void SetupWeEdgeLatch(void)
{
    /* EXTI5 monitors PH5/SDNWE. Unmask the EXTI line for pending capture,
       while keeping the NVIC vector disabled so no IRQ handler is needed. */
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    NVIC_DisableIRQ(EXTI9_5_IRQn);
    MODIFY_REG(SYSCFG->EXTICR[1], 0xFU << 4U, 0x7U << 4U);
    EXTI->IMR1 |= GPIO_PIN_5;
    EXTI->EMR1 &= ~GPIO_PIN_5;
    EXTI->RTSR1 |= GPIO_PIN_5;
    EXTI->FTSR1 |= GPIO_PIN_5;
    EXTI->PR1 = GPIO_PIN_5;
    g_sdram_diag_we_exti_port = (SYSCFG->EXTICR[1] >> 4U) & 0xFU;
    g_sdram_diag_we_exti_imr = ((EXTI->IMR1 & GPIO_PIN_5) != 0U) ? 1U : 0U;
    /* Software-triggered edge proves that the pending flag can latch. */
    EXTI->SWIER1 |= GPIO_PIN_5;
    __DSB();
    g_sdram_diag_we_exti_swtest = ((EXTI->PR1 & GPIO_PIN_5) != 0U) ? 1U : 0U;
    EXTI->PR1 = GPIO_PIN_5;
    __DSB();
}

static uint32_t ReadEdgeMask(void)
{
    uint32_t pending = EXTI->PR1;
    return (((pending & GPIO_PIN_6) != 0U) ? 1U : 0U) |
           (((pending & GPIO_PIN_15) != 0U) ? 2U : 0U) |
           (((pending & GPIO_PIN_11) != 0U) ? 4U : 0U);
}

static void SetupReadEdgeLatches(void)
{
    /* EXTI6 = PH6/SDNE1, EXTI11 = PF11/SDNRAS, EXTI15 = PG15/SDNCAS. */
    NVIC_DisableIRQ(EXTI15_10_IRQn);
    MODIFY_REG(SYSCFG->EXTICR[1], 0xFU << 8U, 0x7U << 8U);
    MODIFY_REG(SYSCFG->EXTICR[2], 0xFU << 12U, 0x5U << 12U);
    MODIFY_REG(SYSCFG->EXTICR[3], 0xFU << 12U, 0x6U << 12U);
    EXTI->IMR1 |= SDRAM_READ_EDGE_PINS;
    EXTI->EMR1 &= ~SDRAM_READ_EDGE_PINS;
    EXTI->RTSR1 |= SDRAM_READ_EDGE_PINS;
    EXTI->FTSR1 |= SDRAM_READ_EDGE_PINS;
    EXTI->PR1 = SDRAM_READ_EDGE_PINS;
    EXTI->SWIER1 |= SDRAM_READ_EDGE_PINS;
    __DSB();
    g_sdram_diag_read_exti_swtest = ReadEdgeMask();
    EXTI->PR1 = SDRAM_READ_EDGE_PINS;
    __DSB();
}

static uint32_t DqEdgeMask(void)
{
    uint32_t pending = EXTI->PR1;
    return (((pending & GPIO_PIN_14) != 0U) ? 1U : 0U) |
           (((pending & GPIO_PIN_10) != 0U) ? 2U : 0U);
}

static void SetupDqEdgeLatches(void)
{
    /* EXTI14 = PD14/FMC_D0, EXTI10 = PD10/FMC_D15. */
    MODIFY_REG(SYSCFG->EXTICR[3], 0xFU << 8U, 0x3U << 8U);
    MODIFY_REG(SYSCFG->EXTICR[2], 0xFU << 8U, 0x3U << 8U);
    EXTI->IMR1 |= SDRAM_DQ_EDGE_PINS;
    EXTI->EMR1 &= ~SDRAM_DQ_EDGE_PINS;
    EXTI->RTSR1 |= SDRAM_DQ_EDGE_PINS;
    EXTI->FTSR1 |= SDRAM_DQ_EDGE_PINS;
    EXTI->PR1 = SDRAM_DQ_EDGE_PINS;
    EXTI->SWIER1 |= SDRAM_DQ_EDGE_PINS;
    __DSB();
    g_sdram_diag_dq_edge_swtest = DqEdgeMask();
    EXTI->PR1 = SDRAM_DQ_EDGE_PINS;
    __DSB();
}

static void ProbeDqEdgePin(uint16_t pin)
{
    GPIO_InitTypeDef gpio = {0};
    HAL_GPIO_WritePin(GPIOD, pin, GPIO_PIN_SET);
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &gpio);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(GPIOD, pin, GPIO_PIN_RESET);
    HAL_Delay(1U);
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOD, &gpio);
}

static void ProbeControlPin(GPIO_TypeDef *port, uint16_t pin, uint32_t result_bit)
{
    GPIO_InitTypeDef gpio = {0};

    /* CKE is still low here, so the SDRAM cannot accept commands. */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    gpio.Pin = pin;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
    HAL_Delay(1U);
    if ((port->IDR & pin) != 0U)
    {
        g_sdram_diag_ctrl_high_mask |= result_bit;
    }

    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
    HAL_Delay(1U);
    if ((port->IDR & pin) == 0U)
    {
        g_sdram_diag_ctrl_low_mask |= result_bit;
    }

    /* Return the line to FMC before sending any SDRAM command. */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(port, &gpio);
    g_sdram_diag_ctrl_af_errors += CheckFmcAfPins(port, pin);
}

static uint32_t CheckDqPulls(GPIO_TypeDef *port, uint32_t pins, uint32_t pull)
{
    uint32_t pin;
    uint32_t errors = 0U;

    for (pin = 0U; pin < 16U; ++pin)
    {
        if ((pins & (1UL << pin)) != 0U)
        {
            if (((port->PUPDR >> (pin * 2U)) & 0x3U) != pull)
            {
                ++errors;
            }
        }
    }
    return errors;
}

static uint32_t CheckFmcAfPins(GPIO_TypeDef *port, uint32_t pin_mask)
{
    uint32_t pin;
    uint32_t errors = 0U;

    for (pin = 0U; pin < 16U; ++pin)
    {
        if ((pin_mask & (1UL << pin)) != 0U)
        {
            uint32_t mode = (port->MODER >> (pin * 2U)) & 0x3U;
            uint32_t af = (port->AFR[pin >> 3U] >> ((pin & 0x7U) * 4U)) & 0xFU;
            if ((mode != GPIO_MODE_AF_PP) || (af != GPIO_AF12_FMC))
            {
                ++errors;
            }
        }
    }
    return errors;
}

static HAL_StatusTypeDef SendSdramCommand(uint32_t mode, uint32_t auto_refresh,
                                          uint32_t mode_register)
{
    FMC_SDRAM_CommandTypeDef command = {0};
    uint32_t timeout = 0x00100000U;

    /* The HAL/LL command helper writes SDCMR but does not wait for BUSY. */
    while ((FMC_Bank5_6_R->SDSR & 0x20U) != 0U)
    {
        if (timeout-- == 0U)
        {
            return HAL_TIMEOUT;
        }
    }

    command.CommandMode = mode;
    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK2;
    command.AutoRefreshNumber = auto_refresh;
    command.ModeRegisterDefinition = mode_register;
    if (HAL_SDRAM_SendCommand(&s_sdram, &command, 0x1000U) != HAL_OK)
    {
        return HAL_ERROR;
    }

    timeout = 0x00100000U;
    while ((FMC_Bank5_6_R->SDSR & 0x20U) != 0U)
    {
        if (timeout-- == 0U)
        {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

uint8_t SDRAM_PrepareClockMHz(uint32_t clock_mhz)
{
    RCC_PeriphCLKInitTypeDef clock = {0};

    if (clock_mhz != 50U && clock_mhz != 100U && clock_mhz != 110U)
    {
        return 0U;
    }
    if (s_sdram.State != HAL_SDRAM_STATE_RESET &&
        HAL_SDRAM_DeInit(&s_sdram) != HAL_OK)
    {
        return 0U;
    }
    /* Stop sourcing FMC from PLL2 while its dividers are changed. No SDRAM
       address may be accessed until SDRAM_Init completes again. */
    clock.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    clock.FmcClockSelection = RCC_FMCCLKSOURCE_HCLK;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    {
        return 0U;
    }
    g_sdram_test_clock_mhz = clock_mhz;
    return 1U;
}

uint8_t SDRAM_Init(void)
{
    FMC_SDRAM_TimingTypeDef timing = {0};

    g_sdram_init_ok = 0U;
    g_sdram_line_scan_done = 0U;

    s_sdram.Instance = FMC_SDRAM_DEVICE;
    s_sdram.Init.SDBank = FMC_SDRAM_BANK2;
    s_sdram.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_10;
    s_sdram.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
    s_sdram.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
    s_sdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
    s_sdram.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
    s_sdram.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    s_sdram.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
    /* Disable FMC read burst during bring-up to keep reads strictly single-beat. */
    s_sdram.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
    s_sdram.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;

    /* Conservative timings for the selected 50/100/110 MHz SDCLK. */
    timing.LoadToActiveDelay = 2U;
    if (g_sdram_test_clock_mhz == 50U)
    {
        timing.ExitSelfRefreshDelay = 5U;
        timing.SelfRefreshTime = 3U;
        timing.RowCycleDelay = 5U;
        timing.WriteRecoveryTime = 2U;
        timing.RPDelay = 2U;
        timing.RCDDelay = 2U;
        s_refresh_count = SDRAM_REFRESH_50MHZ;
    }
    else if (g_sdram_test_clock_mhz == 100U)
    {
        timing.ExitSelfRefreshDelay = 9U;
        timing.SelfRefreshTime = 5U;
        timing.RowCycleDelay = 8U;
        timing.WriteRecoveryTime = 3U;
        timing.RPDelay = 3U;
        timing.RCDDelay = 3U;
        s_refresh_count = SDRAM_REFRESH_100MHZ;
    }
    else
    {
        timing.ExitSelfRefreshDelay = 10U;
        timing.SelfRefreshTime = 6U;
        timing.RowCycleDelay = 9U;
        timing.WriteRecoveryTime = 3U;
        timing.RPDelay = 3U;
        timing.RCDDelay = 3U;
        s_refresh_count = SDRAM_REFRESH_110MHZ;
    }

    s_fmc_clock_ok = 0U;
    if (HAL_SDRAM_Init(&s_sdram, &timing) != HAL_OK ||
        s_fmc_clock_ok == 0U)
    {
        return 0U;
    }

    /* Bit 0: SDNWE/PH5, bit 1: SDNRAS/PF11,
       bit 2: SDNCAS/PG15, bit 3: SDNE1/PH6. */
    g_sdram_diag_ctrl_high_mask = 0U;
    g_sdram_diag_ctrl_low_mask = 0U;
    g_sdram_diag_ctrl_af_errors = 0U;
    SetupWeEdgeLatch();
    SetupReadEdgeLatches();
    SetupDqEdgeLatches();
    ProbeDqEdgePin(GPIO_PIN_14);
    ProbeDqEdgePin(GPIO_PIN_10);
    g_sdram_diag_dq_edge_selftest = DqEdgeMask();
    ProbeControlPin(GPIOH, GPIO_PIN_5, 1UL << 0U);
    g_sdram_diag_we_exti_selftest = ((EXTI->PR1 & GPIO_PIN_5) != 0U) ? 1U : 0U;
    ProbeControlPin(GPIOF, GPIO_PIN_11, 1UL << 1U);
    ProbeControlPin(GPIOG, GPIO_PIN_15, 1UL << 2U);
    ProbeControlPin(GPIOH, GPIO_PIN_6, 1UL << 3U);
    g_sdram_diag_read_exti_selftest = ReadEdgeMask();

    /* PH7 is SDCKE1, the clock-enable signal for FMC SDRAM bank 2. */
    g_sdram_diag_cke_after_fmc_init = GPIOH->IDR & GPIO_PIN_7;
    /* Leave PH7 under FMC control so CLK_ENABLE drives the real SDRAM CKE. */
    g_sdram_diag_ph7_moder_pre_clken = (GPIOH->MODER >> 14U) & 0x3U;
    g_sdram_diag_ph7_odr_pre_clken = GPIOH->ODR & GPIO_PIN_7;
    g_sdram_diag_ph7_idr_pre_clken = GPIOH->IDR & GPIO_PIN_7;
    if (SendSdramCommand(FMC_SDRAM_CMD_CLK_ENABLE, 1U, 0U) != HAL_OK)
    {
        return 0U;
    }
    /* Capture immediately after the command, then again after its settle delay. */
    g_sdram_diag_ph7_moder_post_clken = (GPIOH->MODER >> 14U) & 0x3U;
    g_sdram_diag_ph7_odr_post_clken = GPIOH->ODR & GPIO_PIN_7;
    g_sdram_diag_ph7_idr_post_clken = GPIOH->IDR & GPIO_PIN_7;
    HAL_Delay(1U);
    g_sdram_diag_cke_after_clk_enable = GPIOH->IDR & GPIO_PIN_7;
    if (SendSdramCommand(FMC_SDRAM_CMD_PALL, 1U, 0U) != HAL_OK)
    {
        return 0U;
    }
    g_sdram_diag_cke_after_pall = GPIOH->IDR & GPIO_PIN_7;
    if (SendSdramCommand(FMC_SDRAM_CMD_AUTOREFRESH_MODE, 8U, 0U) != HAL_OK)
    {
        return 0U;
    }
    g_sdram_diag_cke_after_auto_refresh = GPIOH->IDR & GPIO_PIN_7;

    /* Burst length 1, sequential, CAS latency 2, standard mode, single write. */
    if (SendSdramCommand(FMC_SDRAM_CMD_LOAD_MODE, 1U, 0x0220U) != HAL_OK)
    {
        return 0U;
    }
    g_sdram_diag_cke_after_load_mode = GPIOH->IDR & GPIO_PIN_7;
    if (HAL_SDRAM_ProgramRefreshRate(&s_sdram, s_refresh_count) != HAL_OK)
    {
        return 0U;
    }

    g_sdram_diag_sdcr1 = FMC_Bank5_6_R->SDCR[0];
    g_sdram_diag_sdcr2 = FMC_Bank5_6_R->SDCR[1];
    g_sdram_diag_sdtr1 = FMC_Bank5_6_R->SDTR[0];
    g_sdram_diag_sdtr2 = FMC_Bank5_6_R->SDTR[1];
    g_sdram_diag_sdsr = FMC_Bank5_6_R->SDSR;
    g_sdram_diag_sdrtr = FMC_Bank5_6_R->SDRTR;
    g_sdram_diag_sdcmr = FMC_Bank5_6_R->SDCMR;

    /* Diagnostic: bias the 16 FMC data inputs high. If the SDRAM never drives
       the bus, the readback should tend toward ones rather than zeroes. */
    {
        GPIO_InitTypeDef dq_gpio = {0};
        dq_gpio.Mode = GPIO_MODE_AF_PP;
        dq_gpio.Pull = GPIO_PULLUP;
        dq_gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        dq_gpio.Alternate = GPIO_AF12_FMC;
        dq_gpio.Pin = SDRAM_DQ_D_PINS;
        HAL_GPIO_Init(GPIOD, &dq_gpio);
        dq_gpio.Pin = SDRAM_DQ_E_PINS;
        HAL_GPIO_Init(GPIOE, &dq_gpio);
        HAL_Delay(1U);
        g_sdram_diag_dq_pullup_errors =
            CheckDqPulls(GPIOD, SDRAM_DQ_D_PINS, GPIO_PULLUP) +
            CheckDqPulls(GPIOE, SDRAM_DQ_E_PINS, GPIO_PULLUP);
        g_sdram_diag_dq_d_idr = GPIOD->IDR & SDRAM_DQ_D_PINS;
        g_sdram_diag_dq_e_idr = GPIOE->IDR & SDRAM_DQ_E_PINS;
    }

    g_sdram_init_ok = 1U;
    return 1U;
}

static void RecordMismatch(uint32_t address, uint32_t expected, uint32_t actual)
{
    if (g_sdram_error_count == 0U)
    {
        g_sdram_fail_address = address;
        g_sdram_expected_value = expected;
        g_sdram_actual_value = actual;
    }
    ++g_sdram_error_count;
}

static void TestSingleAddress(volatile uint32_t *memory, uint32_t index)
{
    static const uint32_t values[] = {0x12345678U, 0xAAAAAAAAU, 0x55555555U, 0x87654321U};
    uint32_t i;

    for (i = 0U; i < (sizeof(values) / sizeof(values[0])); ++i)
    {
        memory[index] = values[i];
        __DSB();
        if (memory[index] != values[i])
        {
            RecordMismatch(SDRAM_BASE_ADDRESS + index * sizeof(uint32_t), values[i], memory[index]);
            return;
        }
    }
}

/* Probe the physical 16-bit SDRAM data path before the long word test. */
static void TestHalfwordDataPath(void)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    static const uint16_t values[] = {
        0x0000U, 0xFFFFU, 0xAAAAU, 0x5555U, 0xA55AU, 0x5AA5U
    };
    uint32_t i;
    uint16_t actual;

    for (i = 0U; i < (sizeof(values) / sizeof(values[0])); ++i)
    {
        memory[0] = values[i];
        __DSB();
        actual = memory[0];
        g_sdram_probe_lane0[i] = actual;
        if (actual != values[i])
        {
            RecordMismatch(SDRAM_BASE_ADDRESS, values[i], actual);
        }
    }

    /* Exercise the other halfword lane and the next SDRAM word address. */
    for (i = 0U; i < (sizeof(values) / sizeof(values[0])); ++i)
    {
        memory[1] = values[i];
        __DSB();
        actual = memory[1];
        g_sdram_probe_lane1[i] = actual;
        if (actual != values[i])
        {
            RecordMismatch(SDRAM_BASE_ADDRESS + sizeof(uint16_t), values[i], actual);
        }
    }
}

static void SetDqPull(uint32_t pull)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = pull;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;
    gpio.Pin = SDRAM_DQ_D_PINS;
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = SDRAM_DQ_E_PINS;
    HAL_GPIO_Init(GPIOE, &gpio);
}

static void ProbeDqBias(void)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;

    /* A driven SDRAM read should reproduce this pattern under either weak
       GPIO bias. A high-impedance bus instead follows the selected bias. */
    g_sdram_diag_bias_written = 0xA55AU;
    memory[0] = (uint16_t)g_sdram_diag_bias_written;
    __DSB();
    g_sdram_diag_bias_read_up = memory[0];
    SetDqPull(GPIO_PULLDOWN);
    g_sdram_diag_bias_down_gpio_errors =
        CheckDqPulls(GPIOD, SDRAM_DQ_D_PINS, GPIO_PULLDOWN) +
        CheckDqPulls(GPIOE, SDRAM_DQ_E_PINS, GPIO_PULLDOWN) +
        CheckFmcAfPins(GPIOD, SDRAM_DQ_D_PINS) +
        CheckFmcAfPins(GPIOE, SDRAM_DQ_E_PINS);
    __DSB();
    g_sdram_diag_bias_read_down = memory[0];
    SetDqPull(GPIO_PULLUP);
    __DSB();
    g_sdram_diag_bias_read_restored = memory[0];
}

static void ProbeDqmForcedLow(void)
{
    GPIO_InitTypeDef gpio = {0};
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;

    /* DQML=PE0 and DQMH=PE1 are active high masks. Hold both low at the
       MCU pads so the SDRAM data output cannot be masked by FMC NBL state. */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &gpio);
    g_sdram_diag_dqm_forced_mode = GPIOE->MODER & 0xFU;
    g_sdram_diag_dqm_forced_idr = GPIOE->IDR & (GPIO_PIN_0 | GPIO_PIN_1);

    memory[0] = 0xA55AU;
    __DSB();
    g_sdram_diag_dqm_read_up = memory[0];
    SetDqPull(GPIO_PULLDOWN);
    __DSB();
    g_sdram_diag_dqm_read_down = memory[0];
    SetDqPull(GPIO_PULLUP);

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOE, &gpio);
    g_sdram_diag_dqm_restored_af_errors =
        CheckFmcAfPins(GPIOE, GPIO_PIN_0 | GPIO_PIN_1);
}

static void ProbeDqWriteEdges(void)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    volatile uint32_t wait_index;

    EXTI->PR1 = SDRAM_DQ_EDGE_PINS;
    __DSB();
    for (wait_index = 0U; wait_index < 64U; ++wait_index) { __NOP(); }
    g_sdram_diag_dq_edge_idle = DqEdgeMask();
    EXTI->PR1 = SDRAM_DQ_EDGE_PINS;
    __DSB();
    memory[0] = 0x0000U;
    __DSB();
    memory[0] = 0xFFFFU;
    __DSB();
    g_sdram_diag_dq_edge_write = DqEdgeMask();
}

static void TestFullPattern(volatile uint32_t *memory, uint32_t pattern)
{
    uint32_t i;
    uint32_t expected;

    for (i = 0U; i < SDRAM_WORD_COUNT; ++i)
    {
        expected = (pattern == 2U) ? i : ((pattern == 0U) ? 0xAAAAAAAAU : 0x55555555U);
        memory[i] = expected;
    }
    __DSB();
    for (i = 0U; i < SDRAM_WORD_COUNT; ++i)
    {
        expected = (pattern == 2U) ? i : ((pattern == 0U) ? 0xAAAAAAAAU : 0x55555555U);
        if (memory[i] != expected)
        {
            RecordMismatch(SDRAM_BASE_ADDRESS + i * sizeof(uint32_t), expected, memory[i]);
        }
    }
}

void SDRAM_RunBasicTest(void)
{
    volatile uint32_t *memory = (volatile uint32_t *)SDRAM_BASE_ADDRESS;
    volatile uint32_t wait_index;

    g_sdram_test_done = 0U;
    g_sdram_error_count = 0U;
    g_sdram_fail_address = 0U;
    g_sdram_expected_value = 0U;
    g_sdram_actual_value = 0U;

    /* Auto-refresh also pulses CS/CAS. Widen its interval for this brief
       edge probe; COUNT=0 did not read back as zero on this board. */
    (void)HAL_SDRAM_ProgramRefreshRate(&s_sdram, 4096U);
    g_sdram_diag_read_refresh_off = (FMC_Bank5_6_R->SDRTR & FMC_SDRTR_COUNT) >> FMC_SDRTR_COUNT_Pos;
    g_sdram_diag_read_precharge_ok =
        (SendSdramCommand(FMC_SDRAM_CMD_PALL, 1U, 0U) == HAL_OK) ? 1U : 0U;
    for (wait_index = 0U; wait_index < 256U; ++wait_index) { __NOP(); }
    EXTI->PR1 = SDRAM_READ_EDGE_PINS;
    __DSB();
    g_sdram_diag_read_exti_clear = ReadEdgeMask();
    for (wait_index = 0U; wait_index < 64U; ++wait_index) { __NOP(); }
    g_sdram_diag_read_exti_idle = ReadEdgeMask();
    EXTI->PR1 = SDRAM_READ_EDGE_PINS;
    __DSB();
    g_sdram_diag_read_value = *(volatile uint16_t *)SDRAM_BASE_ADDRESS;
    __DSB();
    g_sdram_diag_read_exti_access = ReadEdgeMask();
    (void)HAL_SDRAM_ProgramRefreshRate(&s_sdram, s_refresh_count);
    g_sdram_diag_read_refresh_restored = (FMC_Bank5_6_R->SDRTR & FMC_SDRTR_COUNT) >> FMC_SDRTR_COUNT_Pos;

    /* Clear the initialization-command history. Auto-refresh does not assert
       SDNWE, so a new PH5 edge during this probe comes from a write cycle. */
    EXTI->PR1 = GPIO_PIN_5;
    __DSB();
    TestHalfwordDataPath();
    ProbeDqBias();
    ProbeDqmForcedLow();
    ProbeDqWriteEdges();
    g_sdram_diag_we_exti_access = ((EXTI->PR1 & GPIO_PIN_5) != 0U) ? 1U : 0U;
    /* Sample idle levels at MCU pads after the FMC transactions complete. */
    g_sdram_diag_gpio_b_idr = GPIOB->IDR & (GPIO_PIN_5 | GPIO_PIN_6);
    g_sdram_diag_gpio_h_idr = GPIOH->IDR & (GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
    g_sdram_diag_gpio_f_idr = GPIOF->IDR & GPIO_PIN_11;               /* NRAS */
    g_sdram_diag_gpio_g_idr = GPIOG->IDR & GPIO_PIN_15;               /* NCAS */
    if (g_sdram_error_count != 0U)
    {
        g_sdram_test_done = 1U;
        return;
    }

    TestSingleAddress(memory, 0U);
    TestSingleAddress(memory, SDRAM_WORD_COUNT / 2U);
    TestSingleAddress(memory, SDRAM_WORD_COUNT - 1U);

    TestFullPattern(memory, 0U); /* 0xAAAAAAAA */
    TestFullPattern(memory, 1U); /* 0x55555555 */
    TestFullPattern(memory, 2U); /* incrementing word index */

    g_sdram_test_done = 1U;
}

/* Rework verification: fail fast at the first two halfwords, then exercise
   address decoding before walking the complete 64 MB Bank2 window. */
#define SDRAM_REWORK_CHUNK_WORDS 2048U
static uint32_t s_rework_index;
static uint32_t s_stress_dwell_started_ms;

static uint32_t ReworkWord(uint32_t index)
{
    return 0xA5A55A5AU ^ (index * 0x9E3779B9U);
}

static uint32_t StressWord(uint32_t index, uint32_t pattern, uint32_t round)
{
    uint32_t address_word = ReworkWord(index) ^ (round * 0x01010101U);
    switch (pattern)
    {
        case 0U: return 0x00000000U;
        case 1U: return 0xFFFFFFFFU;
        case 2U: return 0xAAAAAAAAU;
        case 3U: return 0x55555555U;
        case 4U: return index + (round * SDRAM_WORD_COUNT);
        default: return address_word;
    }
}

static void StressFinishPattern(void)
{
    uint32_t total = SDRAM_STRESS_ROUNDS * SDRAM_STRESS_PATTERNS;
    uint32_t round_index = g_sdram_stress_round - 1U;

    g_sdram_stress_round_pass_mask[round_index] |=
        1UL << g_sdram_stress_pattern;
    ++g_sdram_stress_completed_sweeps;
    s_rework_index = 0U;
    g_sdram_test_progress_pct =
        (g_sdram_stress_completed_sweeps * 100U) / total;
    if (g_sdram_stress_completed_sweeps == total)
    {
        g_sdram_test_progress_pct = 100U;
        g_sdram_test_phase = 5U;
        g_sdram_test_done = 1U;
        return;
    }
    ++g_sdram_stress_pattern;
    if (g_sdram_stress_pattern == SDRAM_STRESS_PATTERNS)
    {
        g_sdram_stress_pattern = 0U;
        ++g_sdram_stress_round;
    }
    g_sdram_test_phase = (g_sdram_stress_pattern == 6U) ? 11U : 8U;
}

static void ReworkFail(uint32_t address, uint32_t expected, uint32_t actual)
{
    RecordMismatch(address, expected, actual);
    g_sdram_test_phase = 6U;
    g_sdram_test_done = 1U;
}

static uint8_t ReworkDataTest(void)
{
    static const uint16_t patterns[] = {
        0x0000U, 0xFFFFU, 0xAAAAU, 0x5555U, 0xA55AU, 0x5AA5U
    };
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    uint32_t i;
    uint16_t expected0, expected1, actual;

    for (i = 0U; i < sizeof(patterns) / sizeof(patterns[0]); ++i)
    {
        expected0 = patterns[i];
        expected1 = (uint16_t)~expected0;
        memory[0] = expected0;
        memory[1] = expected1;
        __DSB();
        actual = memory[0];
        if (actual != expected0)
        {
            ReworkFail(SDRAM_BASE_ADDRESS, expected0, actual);
            return 0U;
        }
        actual = memory[1];
        if (actual != expected1)
        {
            ReworkFail(SDRAM_BASE_ADDRESS + 2U, expected1, actual);
            return 0U;
        }
    }

    for (i = 0U; i < 16U; ++i)
    {
        expected0 = (uint16_t)(1U << i);
        expected1 = (uint16_t)~expected0;
        memory[0] = expected0;
        memory[1] = expected1;
        __DSB();
        actual = memory[0];
        if (actual != expected0)
        {
            ReworkFail(SDRAM_BASE_ADDRESS, expected0, actual);
            return 0U;
        }
        actual = memory[1];
        if (actual != expected1)
        {
            ReworkFail(SDRAM_BASE_ADDRESS + 2U, expected1, actual);
            return 0U;
        }
    }
    return 1U;
}

static uint8_t ReworkAddressTest(void)
{
    volatile uint32_t *memory = (volatile uint32_t *)SDRAM_BASE_ADDRESS;
    uint32_t bit, index, expected, actual;

    memory[0] = 0U;
    for (bit = 0U; bit < 24U; ++bit) memory[1UL << bit] = 0U;
    memory[SDRAM_WORD_COUNT - 1U] = 0U;
    __DSB();

    memory[0] = 0xA55A3CC3U;
    for (bit = 0U; bit < 24U; ++bit)
        memory[1UL << bit] = ReworkWord(bit);
    memory[SDRAM_WORD_COUNT - 1U] = 0xDEADBEEFU;
    __DSB();

    actual = memory[0];
    if (actual != 0xA55A3CC3U)
    {
        ReworkFail(SDRAM_BASE_ADDRESS, 0xA55A3CC3U, actual);
        return 0U;
    }
    for (bit = 0U; bit < 24U; ++bit)
    {
        index = 1UL << bit;
        expected = ReworkWord(bit);
        actual = memory[index];
        if (actual != expected)
        {
            ReworkFail(SDRAM_BASE_ADDRESS + index * 4U, expected, actual);
            return 0U;
        }
    }
    actual = memory[SDRAM_WORD_COUNT - 1U];
    if (actual != 0xDEADBEEFU)
    {
        ReworkFail(SDRAM_BASE_ADDRESS + SDRAM_CAPACITY_BYTES - 4U,
                   0xDEADBEEFU, actual);
        return 0U;
    }
    return 1U;
}

static void SetDq4Pull(uint32_t pull)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_7; /* PE7 = FMC_D4. */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = pull;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;
    HAL_GPIO_Init(GPIOE, &gpio);
}

static void ProbeDq4(void)
{
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;

    /* Keep both adjacent columns at zero so this probe does not rely on the
       alternating pattern used by the normal fail-fast data test. */
    memory[0] = 0x0000U;
    memory[1] = 0x0000U;
    memory[128] = 0x0000U; /* 0xD0000100: a different column. */
    __DSB();
    g_sdram_dq4_zero_first = memory[0];
    g_sdram_dq4_zero_repeat = memory[0];

    SetDq4Pull(GPIO_PULLDOWN);
    HAL_Delay(1U);
    g_sdram_dq4_zero_pulldown = memory[0];

    SetDq4Pull(GPIO_PULLUP);
    HAL_Delay(1U);
    g_sdram_dq4_zero_pullup = memory[0];

    SetDq4Pull(GPIO_NOPULL);
    HAL_Delay(1U);
    g_sdram_dq4_zero_far = memory[128];
    memory[0] = 0xFFFFU;
    __DSB();
    g_sdram_dq4_ones = memory[0];
}

static uint32_t CountSetBits(uint32_t value)
{
    uint32_t count = 0U;
    while (value != 0U)
    {
        count += value & 1U;
        value >>= 1U;
    }
    return count;
}

void SDRAM_RunLineScan(void)
{
    static const uint32_t sample_indices[] = {
        0U, 1U, 128U, 1UL << 12U, 1UL << 20U,
        SDRAM_HALFWORD_COUNT - 1U
    };
    static const uint16_t patterns[] = {
        0x0000U, 0xFFFFU, 0xAAAAU, 0x5555U
    };
    volatile uint16_t *memory = (volatile uint16_t *)SDRAM_BASE_ADDRESS;
    volatile uint8_t *bytes = (volatile uint8_t *)SDRAM_BASE_ADDRESS;
    uint32_t sample, pattern, bit, other, index, failed;
    uint16_t expected, actual, usable_bits;

    g_sdram_line_scan_done = 0U;
    g_sdram_dq_data_fail_mask = 0U;
    g_sdram_dq_bias_fail_mask = 0U;
    g_sdram_dq_fail_mask = 0U;
    g_sdram_dq_pass_count = 0U;
    g_sdram_addr_tested_mask = 0U;
    g_sdram_addr_fail_mask = 0U;
    g_sdram_addr_pass_count = 0U;
    g_sdram_addr_last_ok = 0U;
    g_sdram_dqm_low_ok = 0U;
    g_sdram_dqm_high_ok = 0U;
    g_sdram_dqm_tested = 0U;
    g_sdram_dqm_low_readback = 0U;
    g_sdram_dqm_high_readback = 0U;
    SetDqPull(GPIO_NOPULL);

    /* Test every DQ bit at several locations. Both walking-one and
       walking-zero patterns reveal stuck and coupled data lines. */
    for (sample = 0U; sample < sizeof(sample_indices) / sizeof(sample_indices[0]); ++sample)
    {
        index = sample_indices[sample];
        for (pattern = 0U; pattern < sizeof(patterns) / sizeof(patterns[0]); ++pattern)
        {
            expected = patterns[pattern];
            memory[index] = expected;
            __DSB();
            actual = memory[index];
            g_sdram_dq_data_fail_mask |= (uint32_t)(expected ^ actual);
        }
        for (bit = 0U; bit < 16U; ++bit)
        {
            expected = (uint16_t)(1U << bit);
            memory[index] = expected;
            __DSB();
            actual = memory[index];
            g_sdram_dq_data_fail_mask |= (uint32_t)(expected ^ actual);

            expected = (uint16_t)~expected;
            memory[index] = expected;
            __DSB();
            actual = memory[index];
            g_sdram_dq_data_fail_mask |= (uint32_t)(expected ^ actual);
        }

        /* A healthy SDRAM output overcomes the MCU's weak pull. A line
           that follows the pull is likely open or otherwise undriven. */
        memory[index] = 0x0000U;
        __DSB();
        SetDqPull(GPIO_PULLUP);
        HAL_Delay(1U);
        g_sdram_dq_bias_fail_mask |= (uint32_t)memory[index];

        SetDqPull(GPIO_NOPULL);
        memory[index] = 0xFFFFU;
        __DSB();
        SetDqPull(GPIO_PULLDOWN);
        HAL_Delay(1U);
        g_sdram_dq_bias_fail_mask |= (uint32_t)(0xFFFFU ^ memory[index]);
        SetDqPull(GPIO_NOPULL);
    }

    g_sdram_dq_fail_mask = (g_sdram_dq_data_fail_mask |
                            g_sdram_dq_bias_fail_mask) & 0xFFFFU;
    usable_bits = (uint16_t)~g_sdram_dq_fail_mask;
    g_sdram_dq_pass_count = 16U - CountSetBits(g_sdram_dq_fail_mask);

    /* Address checks use only DQ bits that passed the data and bias tests.
       Bit n here means offset 2^n halfwords, not a specific package ball. */
    if (usable_bits != 0U)
    {
        for (bit = 0U; bit < 25U; ++bit)
        {
            memory[0] = 0U;
            for (other = 0U; other < 25U; ++other)
                memory[1UL << other] = 0U;
            memory[SDRAM_HALFWORD_COUNT - 1U] = 0U;
            __DSB();

            memory[1UL << bit] = usable_bits;
            __DSB();
            failed = (((uint16_t)memory[1UL << bit] & usable_bits) != usable_bits) ||
                     (((uint16_t)memory[0] & usable_bits) != 0U) ||
                     (((uint16_t)memory[SDRAM_HALFWORD_COUNT - 1U] & usable_bits) != 0U);
            for (other = 0U; other < 25U; ++other)
            {
                if (other != bit &&
                    (((uint16_t)memory[1UL << other] & usable_bits) != 0U))
                    failed = 1U;
            }
            g_sdram_addr_tested_mask |= 1UL << bit;
            if (failed != 0U) g_sdram_addr_fail_mask |= 1UL << bit;
        }

        memory[0] = 0U;
        for (other = 0U; other < 25U; ++other)
            memory[1UL << other] = 0U;
        memory[SDRAM_HALFWORD_COUNT - 1U] = usable_bits;
        __DSB();
        g_sdram_addr_last_ok =
            (((uint16_t)memory[SDRAM_HALFWORD_COUNT - 1U] & usable_bits) == usable_bits) &&
            (((uint16_t)memory[0] & usable_bits) == 0U);
        for (other = 0U; other < 25U; ++other)
        {
            if (((uint16_t)memory[1UL << other] & usable_bits) != 0U)
                g_sdram_addr_last_ok = 0U;
        }
    }
    g_sdram_addr_pass_count =
        CountSetBits(g_sdram_addr_tested_mask & ~g_sdram_addr_fail_mask);

    /* Byte writes exercise NBL0/NBL1 (DQML/DQMH). Mask out any DQ bits
       already found faulty; both bytes need a good bit to detect a spill. */
    if ((usable_bits & 0x00FFU) != 0U &&
        (usable_bits & 0xFF00U) != 0U)
    {
        g_sdram_dqm_tested = 1U;
        memory[0] = 0x5A3CU;
        __DSB();
        bytes[0] = 0xA5U;
        __DSB();
        g_sdram_dqm_low_readback = memory[0];
        g_sdram_dqm_low_ok =
            ((((uint16_t)g_sdram_dqm_low_readback ^ 0x5AA5U) & usable_bits) == 0U);

        memory[0] = 0x5A3CU;
        __DSB();
        bytes[1] = 0xC3U;
        __DSB();
        g_sdram_dqm_high_readback = memory[0];
        g_sdram_dqm_high_ok =
            ((((uint16_t)g_sdram_dqm_high_readback ^ 0xC33CU) & usable_bits) == 0U);
    }
    SetDqPull(GPIO_NOPULL);
    g_sdram_line_scan_done = 1U;
}

void SDRAM_ReworkTestStart(void)
{
    /* The old probe deliberately enabled weak data pullups. Restore the
       normal FMC pin bias before judging the newly soldered part. */
    SetDqPull(GPIO_NOPULL);
    ProbeDq4();
    g_sdram_test_done = 0U;
    g_sdram_error_count = 0U;
    g_sdram_fail_address = 0U;
    g_sdram_expected_value = 0U;
    g_sdram_actual_value = 0U;
    g_sdram_test_progress_pct = 0U;
    g_sdram_stress_started = 0U;
    g_sdram_stress_round = 0U;
    g_sdram_stress_pattern = 0U;
    g_sdram_stress_completed_sweeps = 0U;
    for (s_rework_index = 0U; s_rework_index < SDRAM_STRESS_ROUNDS;
         ++s_rework_index)
    {
        g_sdram_stress_round_pass_mask[s_rework_index] = 0U;
    }
    s_rework_index = 0U;
    g_sdram_test_phase = 1U;
}

void SDRAM_ReworkTestStep(void)
{
    volatile uint32_t *memory = (volatile uint32_t *)SDRAM_BASE_ADDRESS;
    uint32_t end, expected, actual, index, total_sweeps;

    total_sweeps = SDRAM_STRESS_ROUNDS * SDRAM_STRESS_PATTERNS;

    if (g_sdram_test_phase == 1U)
    {
        if (ReworkDataTest() != 0U) g_sdram_test_phase = 2U;
        return;
    }
    if (g_sdram_test_phase == 2U)
    {
        if (ReworkAddressTest() != 0U) g_sdram_test_phase = 3U;
        return;
    }
    if (g_sdram_test_phase == 9U)
    {
        if ((uint32_t)(HAL_GetTick() - s_stress_dwell_started_ms) >=
            SDRAM_STRESS_DWELL_MS)
        {
            s_rework_index = 0U;
            g_sdram_test_phase = 10U;
        }
        return;
    }
    if (g_sdram_test_phase >= 11U && g_sdram_test_phase <= 16U)
    {
        uint32_t march_phase = g_sdram_test_phase;
        end = s_rework_index + SDRAM_REWORK_CHUNK_WORDS;
        if (end > SDRAM_WORD_COUNT) end = SDRAM_WORD_COUNT;
        while (s_rework_index < end)
        {
            index = (march_phase == 14U || march_phase == 15U) ?
                SDRAM_WORD_COUNT - 1U - s_rework_index : s_rework_index;
            if (march_phase != 11U)
            {
                expected = (march_phase == 13U || march_phase == 15U) ?
                    0xFFFFFFFFU : 0U;
                actual = memory[index];
                if (actual != expected)
                {
                    ReworkFail(SDRAM_BASE_ADDRESS + index * 4U,
                               expected, actual);
                    return;
                }
            }
            if (march_phase != 16U)
            {
                memory[index] =
                    (march_phase == 12U || march_phase == 14U) ?
                    0xFFFFFFFFU : 0U;
            }
            ++s_rework_index;
        }
        __DSB();
        g_sdram_test_progress_pct =
            ((g_sdram_stress_completed_sweeps * 100U) +
             (((march_phase - 11U) * 100U +
               ((s_rework_index * 100U) / SDRAM_WORD_COUNT)) / 6U)) /
            total_sweeps;
        if (s_rework_index == SDRAM_WORD_COUNT)
        {
            s_rework_index = 0U;
            if (march_phase == 16U) StressFinishPattern();
            else g_sdram_test_phase = march_phase + 1U;
        }
        return;
    }
    if (g_sdram_test_phase != 3U && g_sdram_test_phase != 4U &&
        g_sdram_test_phase != 8U && g_sdram_test_phase != 10U) return;

    end = s_rework_index + SDRAM_REWORK_CHUNK_WORDS;
    if (end > SDRAM_WORD_COUNT) end = SDRAM_WORD_COUNT;
    if (g_sdram_test_phase == 3U || g_sdram_test_phase == 8U)
    {
        while (s_rework_index < end)
        {
            memory[s_rework_index] = (g_sdram_test_phase == 3U) ?
                ReworkWord(s_rework_index) :
                StressWord(s_rework_index, g_sdram_stress_pattern,
                           g_sdram_stress_round - 1U);
            ++s_rework_index;
        }
        if (g_sdram_test_phase == 3U)
        {
            g_sdram_test_progress_pct = (s_rework_index * 50U) / SDRAM_WORD_COUNT;
        }
        else
        {
            g_sdram_test_progress_pct =
                ((g_sdram_stress_completed_sweeps * 100U) +
                 ((s_rework_index * 50U) / SDRAM_WORD_COUNT)) / total_sweeps;
        }
        if (s_rework_index == SDRAM_WORD_COUNT)
        {
            __DSB();
            s_rework_index = 0U;
            if (g_sdram_test_phase == 3U)
            {
                g_sdram_test_phase = 4U;
            }
            else
            {
                s_stress_dwell_started_ms = HAL_GetTick();
                g_sdram_test_phase = 9U;
            }
        }
        return;
    }

    while (s_rework_index < end)
    {
        expected = (g_sdram_test_phase == 4U) ?
            ReworkWord(s_rework_index) :
            StressWord(s_rework_index, g_sdram_stress_pattern,
                       g_sdram_stress_round - 1U);
        actual = memory[s_rework_index];
        if (actual != expected)
        {
            ReworkFail(SDRAM_BASE_ADDRESS + s_rework_index * 4U,
                       expected, actual);
            return;
        }
        ++s_rework_index;
    }
    if (g_sdram_test_phase == 4U)
    {
        g_sdram_test_progress_pct = 50U +
            (s_rework_index * 50U) / SDRAM_WORD_COUNT;
    }
    else
    {
        g_sdram_test_progress_pct =
            ((g_sdram_stress_completed_sweeps * 100U) + 50U +
             ((s_rework_index * 50U) / SDRAM_WORD_COUNT)) / total_sweeps;
    }
    if (s_rework_index == SDRAM_WORD_COUNT)
    {
        if (g_sdram_test_phase == 4U)
        {
            g_sdram_stress_started = 1U;
            g_sdram_stress_round = 1U;
            g_sdram_stress_pattern = 0U;
            g_sdram_stress_completed_sweeps = 0U;
            g_sdram_test_progress_pct = 0U;
            s_rework_index = 0U;
            g_sdram_test_phase = 8U;
        }
        else
        {
            StressFinishPattern();
        }
    }
}

/* Diagnostic only: hold the SDCKE1 pad high as GPIO during SDRAM initialization. */
void SDRAM_DiagCkeGpio(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_DeInit(GPIOH, GPIO_PIN_7);
    gpio.Pin = GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOH, &gpio);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_7, GPIO_PIN_SET);
    HAL_Delay(1U);
    g_sdram_diag_ph7_gpio_idr = GPIOH->IDR & GPIO_PIN_7;
}

void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *hsdram)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clock = {0};

    if (hsdram->Instance != FMC_SDRAM_DEVICE)
    {
        return;
    }

    clock.PeriphClockSelection = RCC_PERIPHCLK_FMC | RCC_PERIPHCLK_PLL2_DIVR;
    clock.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
    clock.PLL2.PLL2M = 16U; /* HSI 64 MHz / 16 = 4 MHz PLL input */
    clock.PLL2.PLL2N = (g_sdram_test_clock_mhz == 110U) ? 165U : 150U;
    clock.PLL2.PLL2P = 6U;
    clock.PLL2.PLL2Q = 6U;
    clock.PLL2.PLL2R = (g_sdram_test_clock_mhz == 50U) ? 6U : 3U;
    /* VCO 600/660 MHz, PLL2_R 100/200/220 MHz, FMC SDCLK = PLL2_R/2. */
    clock.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_2;
    clock.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    clock.PLL2.PLL2FRACN = 0U;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    {
        return;
    }
    s_fmc_clock_ok = 1U;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
               GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio); /* D0-D3, D13-D15 */

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
               GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio); /* NBL0-NBL1, D4-D12 */

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
               GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
               GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gpio); /* A0-A9, SDNRAS */

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
               GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gpio); /* A10-A12, BA0-BA1, SDCLK, SDNCAS */

    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOH, &gpio); /* SDNWE, SDNE1, SDCKE1 */

    g_sdram_diag_gpio_af_errors =
        CheckFmcAfPins(GPIOD, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                              GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15) +
        CheckFmcAfPins(GPIOE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |
                              GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 |
                              GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15) +
        CheckFmcAfPins(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                              GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 |
                              GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15) +
        CheckFmcAfPins(GPIOG, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 |
                              GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15) +
        CheckFmcAfPins(GPIOH, GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7);
}
