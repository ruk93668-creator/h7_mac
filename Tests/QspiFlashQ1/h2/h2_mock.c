#include "../behavior_test.c"
#include "h2.c"

uint32_t H2_ControllerOwned(void)
{
    return s_owned ? 1U : 0U;
}

static uint32_t s_test, s_test_opcode, s_test_address, s_test_bytes, s_hal_rx;
static uint32_t s_hal_deinit;
static bool s_stop_tick;

static uint32_t TestTick(void)
{
    return s_stop_tick ? s_tick : ++s_tick;
}

static HAL_StatusTypeDef TestCommand(QSPI_HandleTypeDef *handle, QSPI_CommandTypeDef *command, uint32_t timeout)
{
    (void)handle;
    CHECK(timeout != 0U);
    g_commands++;
    s_test_opcode = command->Instruction;
    s_test_address = command->Address;
    s_test_bytes = command->NbData;
    CHECK(command->InstructionMode == QSPI_INSTRUCTION_1_LINE);
    switch (s_test_opcode)
    {
        case 0x9FU:
        case 0x05U:
        case 0x35U:
        case 0x15U:
            CHECK(command->AddressMode == QSPI_ADDRESS_NONE && command->DataMode == QSPI_DATA_1_LINE);
            CHECK(command->DummyCycles == 0U && s_test_bytes == (s_test_opcode == 0x9FU ? 3U : 1U));
            break;
        case 0x0BU:
        case 0x6BU:
            CHECK(command->AddressMode == QSPI_ADDRESS_1_LINE && command->AddressSize == QSPI_ADDRESS_24_BITS);
            CHECK(command->DataMode == (s_test_opcode == 0x0BU ? QSPI_DATA_1_LINE : QSPI_DATA_4_LINES));
            CHECK(command->DummyCycles == 8U && s_test_address >= H2_FIRST &&
                s_test_address + s_test_bytes <= BSP_QSPI_CAPACITY_BYTES);
            break;
        default:
            CHECK(false);
            return HAL_ERROR;
    }
    QUADSPI->DLR = command->NbData - 1U;
    return HAL_OK;
}

static HAL_StatusTypeDef TestReceive(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    uint32_t i;
    (void)timeout;
    s_hal_rx++;
    handle->RxXferSize = s_test_bytes;
    handle->RxXferCount = 0U;
    handle->State = HAL_QSPI_STATE_READY;
    if (s_test_opcode == 0x9FU)
    {
        data[0] = 0xEFU;
        data[1] = 0x40U;
        data[2] = s_test == 3U && g_qspi_h2_report.phase == H2_RECOVER ? 0x17U : 0x18U;
    }
    else if (s_test_opcode == 0x0BU || s_test_opcode == 0x6BU)
    {
        for (i = 0U; i < s_test_bytes; i++)
            data[i] = (uint8_t)((s_test_address + i) ^ ((s_test_address + i) >> 6U));
        if (g_qspi_h2_report.phase == H2_INJECT)
        {
            CHECK(g_qspi_h2_report.injected == 0U);
            if (s_test == 1U)
                return HAL_ERROR;
            if (s_test == 2U)
                handle->RxXferCount = 1U;
        }
        if (s_test == 6U && s_test_opcode == 0x6BU && g_qspi_h2_report.phase == H2_AFTER)
            data[0] ^= 1U;
    }
    else
        data[0] = s_test_opcode == 0x35U ? 2U : 0U;
    return HAL_OK;
}

static HAL_StatusTypeDef TestDeInit(QSPI_HandleTypeDef *handle)
{
    s_hal_deinit++;
    if (s_test == 5U)
        return HAL_ERROR;
    QUADSPI->CR = QUADSPI->SR = 0U;
    handle->State = HAL_QSPI_STATE_RESET;
    return HAL_OK;
}

int H2_TestRun(uint32_t id)
{
    uint32_t commands, i;
    s_test = id;
    s_hclk = 240000000U;
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 4U);
    s_tick_hook = TestTick;
    s_read_command_hook = TestCommand;
    s_read_receive_hook = TestReceive;
    s_deinit_hook = TestDeInit;
    H2_Boot();
    H2_Step();
    CHECK(g_qspi_h2_report.phase == H2_WAIT_FAULT && g_qspi_h2_arm == 0U && g_init_calls == 0U);
    g_qspi_h2_arm = H2_ARM_RECOVER;
    H2_Step();
    H2_Step();
    CHECK(g_qspi_h2_arm == 0U && g_init_calls == 0U && g_commands == 0U && s_hal_rx == 0U);
    CHECK(!__HAL_RCC_QSPI_IS_CLK_ENABLED() && !__HAL_RCC_GPIOF_IS_CLK_ENABLED());
    if (id == 7U)
    {
        QSPI_CommandTypeDef command = {0};
        static const uint8_t rejected[] = {0x06U, 0x02U, 0x20U, 0x60U, 0xC7U, 0x01U, 0x31U, 0x11U, 0x66U, 0x99U};
        g_qspi_h2_arm = H2_ARM_FAULT;
        H2_Step();
        CHECK(g_qspi_h2_report.phase == H2_WAIT_RECOVER);
        //函数直接调用仅验证白名单入口，最终镜像没有这些指令常量。
        H2_Phase(H2_INJECT);
        commands = g_commands;
        for (i = 0U; i < sizeof(rejected); i++)
        {
            command.Instruction = rejected[i];
            CHECK(H2_Command(&s_qspi, &command, 10U) == HAL_ERROR && g_commands == commands);
        }
        CHECK(g_qspi_h2_report.denied == sizeof(rejected) && g_qspi_h2_report.injected == 1U);
        return (int)g_failure;
    }
    g_qspi_h2_arm = H2_ARM_FAULT;
    H2_Step();
    CHECK(g_qspi_h2_arm == 0U && g_qspi_h2_report.consumed == 1U && g_init_calls == 1U);
    CHECK(g_qspi_h2_report.before_valid == 3U && s_hal_rx != 0U);
    if (id == 1U || id == 2U || id == 5U)
    {
        CHECK(g_qspi_h2_report.phase == H2_DONE && g_qspi_h2_report.complete == 1U);
        CHECK(g_qspi_h2_report.stage[H2_INJECT].fault.fault_latched == 1U);
        CHECK(g_qspi_h2_report.injected == (id == 5U ? 1U : 0U));
        if (id == 2U)
            CHECK(g_qspi_h2_report.fault_read == SYS_ERR_PROTOCOL);
        if (id == 5U)
            CHECK(s_owned && g_qspi_h2_report.stage[H2_INJECT].resources.owned == 1U &&
                __HAL_RCC_QSPI_IS_CLK_ENABLED() &&
                g_qspi_h2_report.stage[H2_INJECT].fault.cleanup_result == SYS_ERR_IO);
    }
    else
    {
        const volatile H2Snapshot_t *fault = &g_qspi_h2_report.stage[H2_FAULT];
        CHECK(g_qspi_h2_report.phase == H2_WAIT_RECOVER && g_qspi_h2_report.restored == 1U);
        CHECK(g_qspi_h2_report.rx_original == HAL_OK && g_qspi_h2_report.rx_returned == HAL_ERROR);
        CHECK(g_qspi_h2_report.rx_size == H2_PROBE_BYTES && g_qspi_h2_report.rx_remaining == 0U);
        CHECK(fault->fault.recovery_attempts == 0U && fault->fault.wren_possible == 0U && !s_owned);
        commands = g_commands;
        g_qspi_h2_arm = H2_ARM_FAULT;
        H2_Step();
        H2_Step();
        CHECK(g_commands == commands && g_qspi_h2_arm == 0U && g_init_calls == 1U);
        s_stop_tick = id == 4U;
        g_qspi_h2_arm = H2_ARM_RECOVER;
        H2_Step();
        CHECK(g_qspi_h2_report.consumed == 3U && g_qspi_h2_report.complete == 1U);
        CHECK(g_qspi_h2_report.injected == 1U && g_qspi_h2_report.denied == 0U);
        if (id == 3U || id == 4U)
        {
            CHECK(g_qspi_h2_report.recover == (id == 3U ? SYS_ERR_PROTOCOL : SYS_ERR_TIMEOUT));
            CHECK(g_qspi_h2_report.stage[H2_RECOVER].fault.fault_latched == 1U);
            CHECK(g_qspi_h2_report.stage[H2_RECOVER].fault.recovery_attempts == 1U);
            CHECK(g_qspi_h2_report.after_valid == 0U && g_init_calls == (id == 3U ? 2U : 1U));
        }
        else if (id == 6U)
        {
            CHECK(g_qspi_h2_report.recover == SYS_OK && g_qspi_h2_report.result == SYS_ERR_PROTOCOL);
            CHECK(g_qspi_h2_report.mismatch_address == H2_FIRST);
        }
        else
        {
            CHECK(g_qspi_h2_report.result == SYS_OK && g_qspi_h2_report.repeat_recover == SYS_ERR_ALREADY_EXISTS);
            CHECK(g_qspi_h2_report.before_valid == 3U && g_qspi_h2_report.after_valid == 3U &&
                g_qspi_h2_report.quad_valid == 3U && g_init_calls == 2U);
            CHECK(memcmp(g_qspi_h2_before, g_qspi_h2_after, H2_BYTES) == 0 && s_hal_deinit == 1U);
        }
    }
    commands = g_commands;
    g_qspi_h2_arm = H2_ARM_RECOVER;
    H2_Step();
    g_qspi_h2_arm = H2_ARM_FAULT;
    H2_Step();
    CHECK(g_commands == commands && g_qspi_h2_arm == 0U && g_qspi_h2_report.denied == 0U);
    return (int)g_failure;
}
