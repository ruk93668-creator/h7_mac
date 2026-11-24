#include "h2.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define H2_TIMEOUT 1000U
#define H2_GATE_MASK 3U
#define H2_STOP_MASK (QUADSPI_CR_EN | QUADSPI_CR_ABORT | QUADSPI_CR_DMAEN | \
    QUADSPI_CR_TEIE | QUADSPI_CR_TCIE | QUADSPI_CR_FTIE | QUADSPI_CR_SMIE | QUADSPI_CR_TOIE)

volatile uint32_t g_qspi_h2_arm;
volatile H2Report_t g_qspi_h2_report;
uint8_t g_qspi_h2_before[H2_BYTES], g_qspi_h2_after[H2_BYTES];
static uint8_t s_quad[BSP_QSPI_SECTOR_BYTES];
static uint8_t s_probe[H2_PROBE_BYTES];
static uint32_t s_commands[H2_OPS], s_received[H2_OPS];
static uint32_t s_op, s_address, s_size;
static QSPI_HandleTypeDef *s_handle;
static const uint8_t s_allowed[H2_OPS] = {0x9FU, 0x05U, 0x35U, 0x15U, 0x0BU, 0x6BU};

//PC 工具按镜像中的布局定位字段，无需猜测 C 结构偏移。
#define H2_OFFSET(type, field) ((uint32_t)offsetof(type, field))
const volatile uint32_t g_qspi_h2_layout[] = {
    H2_SIGNATURE, 1U, sizeof(H2Report_t), H2_OFFSET(H2Report_t, stage), sizeof(H2Snapshot_t),
    H2_OFFSET(H2Snapshot_t, resources), H2_OFFSET(H2Snapshot_t, diagnostics),
    H2_OFFSET(H2Snapshot_t, fault), H2_OFFSET(H2Snapshot_t, commands),
    H2_OFFSET(H2Snapshot_t, received), H2_OFFSET(H2Report_t, before_crc),
    H2_OFFSET(H2Report_t, initial_status)
};

static void H2_Phase(uint32_t phase)
{
    __DMB();
    g_qspi_h2_report.phase = phase;
}

HAL_StatusTypeDef H2_Command(QSPI_HandleTypeDef *handle, QSPI_CommandTypeDef *command, uint32_t timeout)
{
    uint32_t op;
    HAL_StatusTypeDef result;
    uint32_t phase = g_qspi_h2_report.phase;
    s_handle = NULL;
    for (op = 0U; op < H2_OPS && s_allowed[op] != command->Instruction; op++)
    {
    }
    if (op == H2_OPS || g_qspi_h2_report.consumed == 0U ||
        (phase != H2_INIT && phase != H2_NORMAL && phase != H2_INJECT &&
         phase != H2_RECOVER && phase != H2_AFTER))
    {
        g_qspi_h2_report.denied++;
        return HAL_ERROR;
    }
    s_commands[op]++;
    result = HAL_QSPI_Command(handle, command, timeout);
    if (result == HAL_OK)
    {
        s_op = op;
        s_address = command->Address;
        s_size = command->NbData;
        s_handle = handle;
    }
    return result;
}

HAL_StatusTypeDef H2_Receive(QSPI_HandleTypeDef *handle, uint8_t *data, uint32_t timeout)
{
    HAL_StatusTypeDef result;
    bool inject = handle == s_handle && s_allowed[s_op] == 0x0BU && s_address == H2_FIRST &&
        s_size == H2_PROBE_BYTES && g_qspi_h2_report.phase == H2_INJECT &&
        g_qspi_h2_report.injected == 0U;
    if (handle != s_handle)
    {
        g_qspi_h2_report.denied++;
        return HAL_ERROR;
    }
    s_handle = NULL;
    result = HAL_QSPI_Receive(handle, data, timeout);
    if (result == HAL_OK && handle->ErrorCode == HAL_QSPI_ERROR_NONE &&
        handle->RxXferSize == s_size && handle->RxXferCount == 0U)
    {
        s_received[s_op]++;
        if (inject && handle->State == HAL_QSPI_STATE_READY)
        {
            g_qspi_h2_report.rx_original = result;
            g_qspi_h2_report.rx_returned = HAL_ERROR;
            g_qspi_h2_report.rx_error = handle->ErrorCode;
            g_qspi_h2_report.rx_size = handle->RxXferSize;
            g_qspi_h2_report.rx_remaining = handle->RxXferCount;
            g_qspi_h2_report.rx_state = handle->State;
            g_qspi_h2_report.rx_tick = HAL_GetTick();
            g_qspi_h2_report.injected = 1U;
            return HAL_ERROR;
        }
    }
    return result;
}

HAL_StatusTypeDef H2_DeInit(QSPI_HandleTypeDef *handle)
{
    HAL_StatusTypeDef result = HAL_QSPI_DeInit(handle);
    if (g_qspi_h2_report.cleanup_calls++ == 0U)
    {
        g_qspi_h2_report.cleanup_hal = result;
        g_qspi_h2_report.cleanup_state = handle->State;
        g_qspi_h2_report.cleanup_cr = QUADSPI->CR;
        g_qspi_h2_report.cleanup_sr = QUADSPI->SR;
    }
    return result;
}

static void H2_Resources(H2Resources_t *out)
{
    memset(out, 0, sizeof(*out));
    out->tick = HAL_GetTick();
    out->tim_count = TIM6->CNT;
    out->tim_cr1 = TIM6->CR1;
    out->tim_dier = TIM6->DIER;
    out->tim_sr = TIM6->SR;
    out->tim_irq = (NVIC_GetEnableIRQ(TIM6_DAC_IRQn) << 8U) | NVIC_GetPriority(TIM6_DAC_IRQn);
    out->cpu_masks = (__get_PRIMASK() << 16U) | (__get_BASEPRI() << 8U) | __get_FAULTMASK();
    out->qspi_gate = __HAL_RCC_QSPI_IS_CLK_ENABLED() ? 1U : 0U;
    out->owned = H2_ControllerOwned();
    out->qspi_mux = __HAL_RCC_GET_QSPI_SOURCE();
    out->gpio_gates = (__HAL_RCC_GPIOF_IS_CLK_ENABLED() ? 1U : 0U) |
        (__HAL_RCC_GPIOG_IS_CLK_ENABLED() ? 2U : 0U);
    if (out->qspi_gate != 0U)
    {
        out->cr = QUADSPI->CR;
        out->sr = QUADSPI->SR;
    }
    if (out->gpio_gates == H2_GATE_MASK)
    {
        out->pins[0] = GPIOF->MODER & 0x003FF000UL;
        out->pins[1] = GPIOF->OTYPER & 0x7C0UL;
        out->pins[2] = GPIOF->OSPEEDR & 0x003FF000UL;
        out->pins[3] = GPIOF->PUPDR & 0x003FF000UL;
        out->pins[4] = GPIOF->AFR[0] & 0xFF000000UL;
        out->pins[5] = GPIOF->AFR[1] & 0xFFFUL;
        out->pins[6] = GPIOG->MODER & 0x3000UL;
        out->pins[7] = GPIOG->OTYPER & 0x40UL;
        out->pins[8] = GPIOG->OSPEEDR & 0x3000UL;
        out->pins[9] = GPIOG->PUPDR & 0x3000UL;
        out->pins[10] = GPIOG->AFR[0] & 0x0F000000UL;
    }
}

static void H2_Snapshot(uint32_t phase, SysResult_t result)
{
    H2Snapshot_t snapshot = {0};
    snapshot.result = result;
    if (g_qspi_h2_report.consumed != 0U)
    {
        H2_Resources(&snapshot.resources);
        snapshot.diagnostics_result = BSP_QSPI_GetDiagnostics(&snapshot.diagnostics);
        snapshot.fault_result = BSP_QSPI_GetFaultDiagnostics(&snapshot.fault);
    }
    else
    {
        snapshot.resources.tick = HAL_GetTick();
        snapshot.diagnostics_result = SYS_ERR_INVALID_STATE;
        snapshot.fault_result = SYS_ERR_INVALID_STATE;
    }
    memcpy(snapshot.commands, s_commands, sizeof(s_commands));
    memcpy(snapshot.received, s_received, sizeof(s_received));
    snapshot.valid = 1U;
    g_qspi_h2_report.stage[phase] = snapshot;
}

static void H2_Finish(SysResult_t result)
{
    g_qspi_h2_arm = 0U;
    g_qspi_h2_report.result = result;
    H2_Snapshot(H2_DONE, result);
    H2_Phase(H2_DONE);
    __DMB();
    g_qspi_h2_report.complete = 1U;
}

static uint32_t H2_Crc(const uint8_t *data, uint32_t bytes)
{
    uint32_t i, crc = UINT32_MAX;
    while (bytes-- != 0U)
    {
        crc ^= *data++;
        for (i = 0U; i < 8U; i++)
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
    }
    return ~crc;
}

static SysResult_t H2_Read(BspQspiReadMode_t mode, uint32_t address, uint8_t *bytes, uint32_t size)
{
    uint32_t completed;
    SysResult_t result = BSP_QSPI_Read(mode, address, bytes, size, &completed, H2_TIMEOUT);
    return result == SYS_OK && completed != size ? SYS_ERR_PROTOCOL : result;
}

static SysResult_t H2_Compare(const uint8_t *bytes, const uint8_t *reference, uint32_t address)
{
    uint32_t i;
    for (i = 0U; i < BSP_QSPI_SECTOR_BYTES; i++)
        if (bytes[i] != reference[i])
        {
            g_qspi_h2_report.mismatch_address = address + i;
            return SYS_ERR_PROTOCOL;
        }
    return SYS_OK;
}

static SysResult_t H2_Sectors(bool recovered)
{
    uint32_t i;
    uint8_t *capture = recovered ? g_qspi_h2_after : g_qspi_h2_before;
    for (i = 0U; i < 2U; i++)
    {
        uint32_t offset = i * BSP_QSPI_SECTOR_BYTES;
        uint32_t address = H2_FIRST + offset;
        SysResult_t result = H2_Read(BSP_QSPI_READ_FAST, address, &capture[offset], BSP_QSPI_SECTOR_BYTES);
        if (result != SYS_OK)
            return result;
        if (!recovered)
        {
            g_qspi_h2_report.before_crc[i] = H2_Crc(&capture[offset], BSP_QSPI_SECTOR_BYTES);
            g_qspi_h2_report.before_valid |= 1U << i;
            continue;
        }
        g_qspi_h2_report.after_crc[i] = H2_Crc(&capture[offset], BSP_QSPI_SECTOR_BYTES);
        g_qspi_h2_report.after_valid |= 1U << i;
        result = H2_Compare(&capture[offset], &g_qspi_h2_before[offset], address);
        if (result == SYS_OK)
            result = H2_Read(BSP_QSPI_READ_QUAD_OUTPUT, address, s_quad, sizeof(s_quad));
        if (result != SYS_OK)
            return result;
        g_qspi_h2_report.quad_crc[i] = H2_Crc(s_quad, sizeof(s_quad));
        g_qspi_h2_report.quad_valid |= 1U << i;
        result = H2_Compare(s_quad, &g_qspi_h2_before[offset], address);
        if (result != SYS_OK)
            return result;
    }
    return SYS_OK;
}

static SysResult_t H2_Inject(void)
{
    const volatile H2Snapshot_t *shot = &g_qspi_h2_report.stage[H2_INJECT];
    H2Resources_t before, after;
    uint32_t completed, i;
    SysResult_t result;
    H2_Phase(H2_INJECT);
    memset(s_probe, 0xA5, sizeof(s_probe));
    result = BSP_QSPI_Read(BSP_QSPI_READ_FAST, H2_FIRST, s_probe, sizeof(s_probe), &completed, H2_TIMEOUT);
    g_qspi_h2_report.fault_read = result;
    g_qspi_h2_report.completed = completed;
    H2_Snapshot(H2_INJECT, result);
    if (result != SYS_ERR_IO || g_qspi_h2_report.injected != 1U || completed != 0U ||
        shot->diagnostics_result != SYS_OK || shot->fault_result != SYS_OK ||
        shot->diagnostics.ready != 0U || shot->diagnostics.fault != 1U ||
        shot->fault.fault_origin != BSP_QSPI_FAULT_READ || shot->fault.fault_latched != 1U ||
        shot->fault.fault_stage != BSP_QSPI_READ_STAGE_RECEIVE || shot->fault.hal_status != HAL_ERROR ||
        shot->fault.primary_result != SYS_ERR_IO || shot->fault.fault_opcode != 0x0BU ||
        shot->fault.wren_possible != 0U || shot->fault.mutation_possible != 0U ||
        shot->fault.flash_completion_unknown != 0U || g_qspi_h2_report.denied != 0U)
        return SYS_ERR_PROTOCOL;
    for (i = 0U; i < sizeof(s_probe); i++)
        if (s_probe[i] != 0xA5U)
            return SYS_ERR_PROTOCOL;
    before = g_qspi_h2_report.initial_resources;
    after = shot->resources;
    if (shot->fault.cleanup_result != SYS_OK || shot->fault.cleanup_stage != BSP_QSPI_CLEANUP_COMPLETE ||
        shot->fault.controller_idle_verified != 1U || after.qspi_gate != 0U || after.owned != 0U ||
        g_qspi_h2_report.cleanup_calls != 1U || g_qspi_h2_report.cleanup_hal != HAL_OK ||
        g_qspi_h2_report.cleanup_state != HAL_QSPI_STATE_RESET ||
        (g_qspi_h2_report.cleanup_cr & H2_STOP_MASK) != 0U ||
        (g_qspi_h2_report.cleanup_sr & QUADSPI_SR_BUSY) != 0U ||
        before.qspi_gate != 0U || after.gpio_gates != H2_GATE_MASK ||
        before.qspi_mux != after.qspi_mux || memcmp(before.pins, after.pins, sizeof(before.pins)) != 0)
        return SYS_ERR_IO;
    g_qspi_h2_report.restored = 1U;
    H2_Phase(H2_FAULT);
    g_qspi_h2_report.blocked_init = BSP_QSPI_Init();
    g_qspi_h2_report.blocked_read = BSP_QSPI_Read(BSP_QSPI_READ_FAST, H2_FIRST, s_probe,
        sizeof(s_probe), &completed, H2_TIMEOUT);
    g_qspi_h2_report.blocked_completed = completed;
    result = SYS_OK;
    if (g_qspi_h2_report.blocked_init != SYS_ERR_INVALID_STATE ||
        g_qspi_h2_report.blocked_read != SYS_ERR_INVALID_STATE || completed != 0U ||
        g_qspi_h2_report.denied != 0U)
        result = SYS_ERR_PROTOCOL;
    H2_Snapshot(H2_FAULT, result);
    return result;
}

static SysResult_t H2_FirstStage(void)
{
    H2Resources_t initial;
    BspQspiStatus_t status;
    uint32_t id;
    SysResult_t result;
    H2_Phase(H2_INIT);
    //仅授权后打开 GPIO 门作基线读回。BSP 清理也保留这两个共享门。
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    H2_Resources(&initial);
    g_qspi_h2_report.initial_resources = initial;
    result = BSP_QSPI_Init();
    g_qspi_h2_report.init = result;
    if (result == SYS_OK)
    {
        result = BSP_QSPI_GetId(&id);
        g_qspi_h2_report.id_result = result;
        if (result == SYS_OK)
        {
            g_qspi_h2_report.id = id;
            result = id == BSP_QSPI_EXPECTED_ID ? BSP_QSPI_ReadStatus(&status, H2_TIMEOUT) : SYS_ERR_PROTOCOL;
            g_qspi_h2_report.status_result = result;
            if (result == SYS_OK)
            {
                g_qspi_h2_report.initial_status = status;
                if (status.busy != 0U || status.sus != 0U || status.wel != 0U)
                    result = SYS_ERR_INVALID_STATE;
            }
        }
    }
    H2_Snapshot(H2_INIT, result);
    if (result != SYS_OK)
        return result;
    if (g_qspi_h2_report.stage[H2_INIT].diagnostics.ready != 1U ||
        g_qspi_h2_report.stage[H2_INIT].diagnostics.serial_clock_hz != 120000000U)
        return SYS_ERR_PROTOCOL;
    H2_Phase(H2_NORMAL);
    result = H2_Read(BSP_QSPI_READ_FAST, H2_FIRST, s_probe, sizeof(s_probe));
    if (result == SYS_OK)
        result = H2_Sectors(false);
    H2_Snapshot(H2_NORMAL, result);
    return result == SYS_OK ? H2_Inject() : result;
}

static SysResult_t H2_RecoveryStage(void)
{
    const volatile H2Snapshot_t *shot = &g_qspi_h2_report.stage[H2_RECOVER];
    const volatile H2Snapshot_t *fault = &g_qspi_h2_report.stage[H2_FAULT];
    SysResult_t result;
    uint32_t i, id;
    H2_Phase(H2_RECOVER);
    g_qspi_h2_report.recover_entry_tim_count = TIM6->CNT;
    g_qspi_h2_report.recover_entry_tick = HAL_GetTick();
    result = BSP_QSPI_Recover();
    g_qspi_h2_report.recover = result;
    H2_Snapshot(H2_RECOVER, result);
    if (result != SYS_OK)
        return result;
    if (shot->diagnostics_result != SYS_OK || shot->fault_result != SYS_OK ||
        shot->fault.recovery_stage != BSP_QSPI_RECOVERY_READY || shot->fault.recovery_result != SYS_OK ||
        shot->fault.recovery_attempts != 1U || shot->fault.fault_latched != 0U ||
        shot->fault.fault_origin != BSP_QSPI_FAULT_READ || shot->fault.primary_result != SYS_ERR_IO ||
        shot->fault.controller_idle_verified != 1U || shot->fault.flash_idle_verified != 1U ||
        shot->diagnostics.ready != 1U || shot->diagnostics.fault != 0U ||
        shot->resources.qspi_gate != 1U || shot->resources.owned != 1U ||
        shot->diagnostics.status_valid_mask != 7U || shot->diagnostics.status.busy != 0U ||
        shot->diagnostics.status.sus != 0U || shot->diagnostics.status.wel != 0U)
        return SYS_ERR_PROTOCOL;
    for (i = 0U; i < H2_OPS; i++)
        if (shot->commands[i] - fault->commands[i] != (i < 4U ? 1U : 0U) ||
            shot->received[i] - fault->received[i] != (i < 4U ? 1U : 0U))
            return SYS_ERR_PROTOCOL;
    result = BSP_QSPI_GetId(&id);
    g_qspi_h2_report.recovered_id_result = result;
    if (result != SYS_OK)
        return result;
    g_qspi_h2_report.recovered_id = id;
    if (id != BSP_QSPI_EXPECTED_ID)
        return SYS_ERR_PROTOCOL;
    H2_Phase(H2_AFTER);
    result = H2_Sectors(true);
    H2_Snapshot(H2_AFTER, result);
    if (result != SYS_OK)
        return result;
    //已 READY 的无操作契约检查。任何实际恢复失败均直接停止。
    g_qspi_h2_report.repeat_recover = BSP_QSPI_Recover();
    return g_qspi_h2_report.repeat_recover == SYS_ERR_ALREADY_EXISTS &&
        g_qspi_h2_report.denied == 0U ? SYS_OK : SYS_ERR_PROTOCOL;
}

void H2_Boot(void)
{
    g_qspi_h2_arm = 0U;
    g_qspi_h2_report.signature = g_qspi_h2_layout[0];
    g_qspi_h2_report.version = g_qspi_h2_layout[1];
    g_qspi_h2_report.size = sizeof(H2Report_t);
    g_qspi_h2_report.result = SYS_ERR_INVALID_STATE;
    g_qspi_h2_report.mismatch_address = UINT32_MAX;
}

void H2_BootFailure(void)
{
    H2_Finish(SYS_ERR_INVALID_STATE);
}

void H2_Step(void)
{
    SysResult_t result;
    uint32_t phase = g_qspi_h2_report.phase;
    if (phase == H2_BOOT)
    {
        H2_Snapshot(H2_BOOT, SYS_OK);
        H2_Phase(H2_WAIT_FAULT);
        H2_Snapshot(H2_WAIT_FAULT, SYS_OK);
    }
    else if (phase == H2_WAIT_FAULT && g_qspi_h2_arm != 0U)
    {
        uint32_t token = g_qspi_h2_arm;
        g_qspi_h2_arm = 0U;
        if (token != H2_ARM_FAULT)
            return;
        g_qspi_h2_report.consumed |= 1U;
        result = H2_FirstStage();
        if (result != SYS_OK)
            H2_Finish(result);
        else
        {
            g_qspi_h2_arm = 0U;
            H2_Phase(H2_WAIT_RECOVER);
            H2_Snapshot(H2_WAIT_RECOVER, SYS_OK);
        }
    }
    else if (phase == H2_WAIT_RECOVER && g_qspi_h2_arm != 0U)
    {
        uint32_t token = g_qspi_h2_arm;
        g_qspi_h2_arm = 0U;
        if (token != H2_ARM_RECOVER)
            return;
        g_qspi_h2_report.consumed |= 2U;
        H2_Finish(H2_RecoveryStage());
    }
    else if (phase == H2_DONE)
        g_qspi_h2_arm = 0U;
}
