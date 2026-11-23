//只通过生产 API 形成故障和恢复。注入点限定在 HAL 与模拟寄存器。
#include "write_test.c"

static uint32_t s_q14_case, s_last_instruction, s_cmd_9f, s_status_calls, s_q14_abort, s_q14_deinit;
static uint8_t s_recovery_opcodes[4];
static uint32_t s_recovery_commands;
static bool s_recovery_probe;
static const HAL_StatusTypeDef s_failures[] = {HAL_BUSY, HAL_ERROR, HAL_TIMEOUT};
static const SysResult_t s_results[] = {SYS_ERR_BUSY, SYS_ERR_IO, SYS_ERR_TIMEOUT};

static HAL_StatusTypeDef Q14Command(QSPI_HandleTypeDef *handle, QSPI_CommandTypeDef *cmd, uint32_t timeout)
{
    s_last_instruction = cmd->Instruction;
    if (s_recovery_probe && s_recovery_commands < sizeof(s_recovery_opcodes))
        s_recovery_opcodes[s_recovery_commands++] = (uint8_t)s_last_instruction;
    if (s_last_instruction == 0x9FU)
    {
        BspQspiFaultDiagnostics_t fault;
        s_cmd_9f++;
        CHECK(s_recovery_probe && cmd->AddressMode == QSPI_ADDRESS_NONE && cmd->NbData == 3U);
        if (s_q14_case == 16U)
        {
            CHECK(BSP_QSPI_Recover() == SYS_ERR_BUSY);
            CHECK(BSP_QSPI_GetFaultDiagnostics(&fault) == SYS_ERR_BUSY);
        }
        QUADSPI->DLR = 2U;
        g_commands++;
        return HAL_OK;
    }
    if (s_last_instruction == 0x05U || s_last_instruction == 0x35U || s_last_instruction == 0x15U)
        s_status_calls++;
    if (s_last_instruction == 0x0BU && !s_recovery_probe && s_q14_case < 3U)
    {
        g_commands++;
        return s_failures[s_q14_case];
    }
    if (s_last_instruction == 0x02U && s_q14_case >= 28U && s_q14_case <= 30U)
    {
        CHECK(WriteModelCommand(handle, cmd, timeout) == HAL_OK);
        return s_failures[s_q14_case - 28U];
    }
    return WriteModelCommand(handle, cmd, timeout);
}

static HAL_StatusTypeDef Q14Receive(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    HAL_StatusTypeDef result;
    if (s_last_instruction == 0x9FU)
    {
        data[0] = 0xEFU; data[1] = 0x40U; data[2] = 0x18U;
        handle->RxXferSize = 3U; handle->RxXferCount = 0U;
        return HAL_OK;
    }
    result = WriteModelReceive(handle, data, timeout);
    if (s_recovery_probe && s_q14_case == 17U && s_last_instruction == 0x35U)
        return HAL_ERROR;
    if (!s_recovery_probe && s_last_instruction == 0x0BU)
    {
        if (s_q14_case >= 3U && s_q14_case <= 5U)
            return s_failures[s_q14_case - 3U];
        if (s_q14_case == 6U)
            handle->ErrorCode = HAL_QSPI_ERROR_TRANSFER;
        if (s_q14_case == 7U)
            handle->RxXferCount = 1U;
        if (s_q14_case == 8U)
            QUADSPI->DCR ^= 1U;
        if (s_q14_case == 39U && s_mutations != 0U)
            s_tick += 2500U;
        if (s_q14_case == 42U)
            QUADSPI->CR |= QUADSPI_CR_DMAEN;
    }
    return result;
}

static HAL_StatusTypeDef Q14Transmit(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    HAL_StatusTypeDef result = WriteModelTransmit(handle, data, timeout);
    if (s_q14_case >= 31U && s_q14_case <= 33U)
        return s_failures[s_q14_case - 31U];
    if (s_q14_case == 38U)
        s_tick += 2500U;
    return result;
}

static HAL_StatusTypeDef Q14Abort(QSPI_HandleTypeDef *handle)
{
    s_q14_abort++;
    if (!s_recovery_probe)
    {
        if (s_q14_case == 10U)
            return HAL_ERROR;
        if (s_q14_case == 9U)
            return HAL_OK;  //控制器仍 BUSY。
        if (s_q14_case == 40U)
            handle->ErrorCode = HAL_QSPI_ERROR_TRANSFER;
    }
    QUADSPI->SR = 0U;
    return HAL_OK;
}

static HAL_StatusTypeDef Q14Deinit(QSPI_HandleTypeDef *handle)
{
    s_q14_deinit++;
    if (!s_recovery_probe && s_q14_case == 11U)
        return HAL_ERROR;
    if (!s_recovery_probe && s_q14_case == 41U)
        return HAL_OK;  //句柄和控制器未复位。
    QUADSPI->CR = 0U;
    handle->State = HAL_QSPI_STATE_RESET;
    return HAL_OK;
}

static uint32_t Q14FrozenTick(void)
{
    return s_tick;
}

static void Q14InstallHooks(void)
{
    s_read_command_hook = Q14Command;
    s_read_receive_hook = Q14Receive;
    s_write_transmit_hook = Q14Transmit;
    s_read_abort_hook = Q14Abort;
    s_deinit_hook = Q14Deinit;
}

static void Q14CheckLocked(void)
{
    uint8_t data;
    uint32_t bytes, cached, commands = g_commands;
    BspQspiWriteDiagnostics_t wr;
    CHECK(BSP_QSPI_Init() == SYS_ERR_INVALID_STATE);
    CHECK(BSP_QSPI_Read(BSP_QSPI_READ_FAST, MODEL_BASE, &data, 1U, &bytes, 200U) ==
        SYS_ERR_INVALID_STATE && bytes == 0U);
    CHECK(BSP_QSPI_GetId(&cached) == SYS_ERR_INVALID_STATE);
    CHECK(Qspi_ProgramWithPolicy(&s_model, MODEL_BASE, s_pattern, 1U, 500U, &wr) ==
        SYS_ERR_INVALID_STATE && wr.completed == 0U);
    CHECK(Qspi_EraseWithPolicy(&s_model, MODEL_BASE, s_array, 4096U, 3000U, &wr) ==
        SYS_ERR_INVALID_STATE && wr.completed == 0U);
    CHECK(g_commands == commands);
}

static int Q14InitFault(uint32_t id)
{
    BspQspiFaultDiagnostics_t fault;
    s_hclk = 240000000U;
    TIM6->CR1 = TIM_CR1_CEN;
    TIM6->DIER = TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    NVIC_SetPriority(TIM6_DAC_IRQn, 4U);
    s_case = id == 23U ? 3U : (id == 24U ? 6U : 10U);
    CHECK(BSP_QSPI_Init() == (id == 25U ? SYS_ERR_PROTOCOL : SYS_ERR_IO));
    CHECK(BSP_QSPI_GetFaultDiagnostics(&fault) == SYS_OK);
    CHECK(fault.fault_origin == (id == 23U ? BSP_QSPI_FAULT_PREPARE :
        (id == 24U ? BSP_QSPI_FAULT_HAL_INIT : BSP_QSPI_FAULT_INIT_IDENTIFY)));
    CHECK(!s_owned && fault.fault_latched == (id == 23U ? 0U : 1U));
    if (id == 23U)
    {
        CHECK(BSP_QSPI_Recover() == SYS_ERR_INVALID_STATE && g_commands == 0U);
        s_case = 0U;
        CHECK(BSP_QSPI_Init() == SYS_OK);
    }
    else
    {
        Q14CheckLocked();
        s_case = 0U;
        s_recovery_probe = true;
        Q14InstallHooks();
        CHECK(BSP_QSPI_Recover() == SYS_OK && s_cmd_9f == 1U && s_status_calls == 3U);
        CHECK(BSP_QSPI_GetFaultDiagnostics(&fault) == SYS_OK);
        CHECK(fault.fault_latched == 0U && fault.flash_idle_verified == 1U &&
            fault.primary_result == (id == 25U ? SYS_ERR_PROTOCOL : SYS_ERR_IO));
    }
    return (int)g_failure;
}

int Q14_TestRun(uint32_t id)
{
    uint8_t data;
    uint32_t confirmed, commands, mutations, wren;
    BspQspiWriteDiagnostics_t wr;
    BspQspiFaultDiagnostics_t initial, after;
    SysResult_t result, expected, recovery_expected = SYS_OK;
    BspQspiFaultOrigin_t origin = BSP_QSPI_FAULT_READ;
    s_q14_case = id;
    if (id >= 23U && id <= 25U)
        return Q14InitFault(id);
    WriteModelInit(id == 26U ? 20U : id == 27U ? 19U : id == 34U ? 17U :
        id == 35U ? 8U : id == 36U ? 9U : id == 37U ? 10U : id == 44U ? 34U : 0U);
    Q14InstallHooks();
    if (id == 9U)
        QUADSPI->SR = QUADSPI_SR_BUSY;
    if (id >= 26U && id <= 39U)
    {
        result = Qspi_ProgramWithPolicy(&s_model, MODEL_BASE + 255U, s_pattern, 520U, 2000U, &wr);
        expected = id == 27U ? SYS_ERR_IO : id >= 28U && id <= 33U ? s_results[(id-28U)%3U] :
            id == 34U || id == 36U ? SYS_ERR_PROTOCOL : SYS_ERR_TIMEOUT;
        origin = id == 26U ? BSP_QSPI_FAULT_WRITE_PREFLIGHT : id == 27U ? BSP_QSPI_FAULT_WREN :
            id == 35U || id == 38U ? BSP_QSPI_FAULT_FLASH_PENDING :
            id == 36U || id == 39U ? BSP_QSPI_FAULT_WRITE_VERIFY : BSP_QSPI_FAULT_MUTATION_COMMAND;
        CHECK(wr.completed == (id == 37U ? 1U : 0U));
    }
    else if (id == 44U)
    {
        static uint8_t backup[4096];
        memcpy(backup,s_array,sizeof(backup));
        result = Qspi_EraseWithPolicy(&s_model, MODEL_BASE, backup, sizeof(backup), 2000U, &wr);
        origin = BSP_QSPI_FAULT_FLASH_PENDING;
        expected = SYS_ERR_TIMEOUT;
        CHECK(wr.completed == 0U && wr.flash_completion_unknown == 1U);
    }
    else if (id == 11U || id == 40U || id == 41U)
    {
        result = BSP_QSPI_DeInit();
        expected = SYS_ERR_IO;
        origin = BSP_QSPI_FAULT_CLEANUP;
    }
    else
    {
        if (id >= 10U && id != 42U)
            QUADSPI->DCR ^= 1U;
        result = BSP_QSPI_Read(BSP_QSPI_READ_FAST, MODEL_BASE, &data, 1U, &confirmed, 200U);
        expected = id < 6U ? s_results[id%3U] : id == 6U ? SYS_ERR_IO :
            id == 7U ? SYS_ERR_PROTOCOL : SYS_ERR_INVALID_STATE;
        CHECK(confirmed==0U);
    }
    CHECK(result == expected);
    CHECK(BSP_QSPI_GetFaultDiagnostics(&initial) == SYS_OK);
    CHECK(initial.fault_latched == 1U && initial.fault_origin == origin && initial.primary_result == expected);
    CHECK(initial.confirmed_write_bytes == (id == 37U ? 1U : 0U));
    if (id == 37U)
        CHECK(initial.confirmed_write_address == MODEL_BASE+255U && initial.confirmed_write_opcode == 2U);
    if (id == 9U || id == 10U || id == 11U || id == 40U || id == 41U || id == 42U)
    {
        CHECK(initial.cleanup_result != SYS_OK && s_owned && __HAL_RCC_QSPI_IS_CLK_ENABLED());
        CHECK(initial.cleanup_stage == (id == 42U ? BSP_QSPI_CLEANUP_CHECK :
            (id == 11U || id == 41U ? BSP_QSPI_CLEANUP_DEINIT : BSP_QSPI_CLEANUP_ABORT)));
    }
    else
        CHECK(initial.cleanup_result == SYS_OK && !s_owned && !__HAL_RCC_QSPI_IS_CLK_ENABLED());
    if (id == 6U)
        CHECK(initial.hal_status == HAL_OK && initial.hal_error == HAL_QSPI_ERROR_TRANSFER);
    if (id == 36U || id == 39U)
        CHECK(initial.flash_idle_verified == 1U && initial.flash_completion_unknown == 0U);
    if (id == 27U)
        CHECK(initial.wren_possible == 1U && initial.mutation_possible == 0U && initial.flash_completion_unknown == 0U);
    if ((id >= 28U && id <= 38U && id != 36U) || id == 44U)
        CHECK(initial.mutation_possible == 1U && initial.flash_completion_unknown == 1U);
    Q14CheckLocked();
    commands = g_commands; mutations = s_mutations; wren = s_wren;
    s_recovery_probe = true;
    if (id == 26U) s_scenario = 0U;
    if (id == 12U) {s_sr1=1U; recovery_expected=SYS_ERR_BUSY;}
    if (id == 13U) {s_sr2=0x80U; recovery_expected=SYS_ERR_INVALID_STATE;}
    if (id == 14U) {s_wel=1U; recovery_expected=SYS_ERR_INVALID_STATE;}
    if (id == 17U) recovery_expected=SYS_ERR_IO;
    if (id == 18U) s_tick=UINT32_MAX-1U;
    if (id == 19U) {s_case=32U; recovery_expected=SYS_ERR_TIMEOUT;}
    if (id == 20U) {s_tick_hook=Q14FrozenTick; recovery_expected=SYS_ERR_TIMEOUT;}
    if (id == 21U) {GPIOF->MODER ^= (1UL<<12U); recovery_expected=SYS_ERR_BUSY;}
    if (id == 22U) {__HAL_RCC_QSPI_CLK_ENABLE(); recovery_expected=SYS_ERR_BUSY;}
    if ((id >= 27U && id <= 39U) || id == 44U) recovery_expected=SYS_ERR_NOT_SUPPORTED;
    if (id == 42U) recovery_expected=SYS_ERR_INVALID_STATE;
    CHECK(BSP_QSPI_Recover() == recovery_expected);
    CHECK(BSP_QSPI_GetFaultDiagnostics(&after) == SYS_OK);
    CHECK(after.recovery_attempts == 1U && after.recovery_result == recovery_expected);
    CHECK(after.primary_result==initial.primary_result && after.fault_origin==initial.fault_origin &&
        after.cleanup_result==initial.cleanup_result && after.cleanup_stage==initial.cleanup_stage);
    CHECK(s_mutations==mutations && s_wren==wren);
    if (recovery_expected == SYS_OK)
    {
        CHECK(after.recovery_stage == BSP_QSPI_RECOVERY_READY && after.fault_latched == 0U);
        CHECK(after.controller_idle_verified == 1U && after.flash_idle_verified == 1U);
        CHECK(s_owned && __HAL_RCC_QSPI_IS_CLK_ENABLED() && s_cmd_9f == 1U);
        CHECK(g_commands == commands + 4U && s_recovery_commands == 4U &&
            s_recovery_opcodes[0] == 0x9FU && s_recovery_opcodes[1] == 0x05U &&
            s_recovery_opcodes[2] == 0x35U && s_recovery_opcodes[3] == 0x15U);
        CHECK(BSP_QSPI_Read(BSP_QSPI_READ_FAST,MODEL_BASE,&data,1U,&confirmed,200U)==SYS_OK && confirmed==1U);
        CHECK(BSP_QSPI_Recover()==SYS_ERR_ALREADY_EXISTS);
    }
    else
    {
        CHECK(after.fault_latched==1U && s_diag.ready==0U);
        if (recovery_expected==SYS_ERR_NOT_SUPPORTED)
        {
            CHECK(after.recovery_stage==BSP_QSPI_RECOVERY_BLOCKED && g_commands==commands);
            CHECK(BSP_QSPI_Recover()==SYS_ERR_NOT_SUPPORTED && g_commands==commands);
        }
        if (id==20U || id==21U || id==22U || id==42U)
            CHECK(g_commands==commands);
        if (id==12U || id==13U || id==14U || id==17U)
            CHECK(after.recovery_stage==BSP_QSPI_RECOVERY_STATUS && after.flash_idle_verified==0U);
        Q14CheckLocked();
        if (id==12U)
        {
            s_sr1=0U;
            CHECK(BSP_QSPI_Recover()==SYS_OK);
            CHECK(BSP_QSPI_GetFaultDiagnostics(&after)==SYS_OK && after.recovery_attempts==2U);
        }
    }
    return (int)g_failure;
}
