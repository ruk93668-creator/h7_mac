#include "bsp_bt_uart.h"
#include "bt_hci_h5.h"
#include "framework_init.h"
#include "FreeRTOS.h"
#include "task.h"

#define PROBE_COMMAND_TIMEOUT_MS 2500U
#define PROBE_LINK_TIMEOUT_MS 7000U
#define HCI_READ_LOCAL_VERSION 0x1001U
#define HCI_READ_ROM_VERSION 0xfc6dU

typedef enum
{
    BLE_PROBE_IDLE, BLE_PROBE_LINK, BLE_PROBE_LOCAL_VERSION,
    BLE_PROBE_ROM_VERSION, BLE_PROBE_DRAIN, BLE_PROBE_DONE, BLE_PROBE_FAILED
} BleProbeStage_t;

typedef struct
{
    BleProbeStage_t stage, failed_stage;
    SysResult_t result;
    uint32_t expected_opcode, last_event, last_status, rx_bytes, uart_fault;
    uint32_t h5_state, h5_attempts, h5_rx_frames, h5_tx_frames;
    uint32_t h5_header_errors, h5_framing_errors, h5_crc_errors, h5_retransmits;
    uint32_t hci_version, hci_revision, lmp_version, manufacturer, lmp_subversion, rom_version;
    uint32_t local_event_size, rom_event_size;
    uint8_t local_event[14], rom_event[7];
} BleProbeDebug_t;

volatile BleProbeDebug_t g_ble_probe;
static BtH5_t s_link;
static StaticTask_t s_task;
static StackType_t s_stack[512] __attribute__((aligned(8)));
static bool s_reply;
static SysResult_t s_reply_result;

static uint16_t Probe_Le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static SysResult_t Probe_Write(void *context, const uint8_t *bytes, uint32_t length, uint32_t *count)
{
    (void)context;
    return BSP_BT_UART_Write(bytes, length, count);
}

static SysResult_t Probe_Receive(void *context, uint8_t type, const uint8_t *bytes, uint32_t length)
{
    uint16_t opcode;
    uint32_t i;
    (void)context;
    if (type != 4U || length < 2U)
        return SYS_OK;
    g_ble_probe.last_event = bytes[0];
    if (bytes[0] == 0x0fU && length == 6U && bytes[1] == 4U &&
        Probe_Le16(bytes + 4U) == g_ble_probe.expected_opcode && bytes[2] != 0U)
    {
        g_ble_probe.last_status = bytes[2];
        s_reply_result = SYS_ERR_IO;
        s_reply = true;
        return SYS_OK;
    }
    if (bytes[0] != 0x0eU || length < 5U || s_reply)
        return SYS_OK;
    opcode = Probe_Le16(bytes + 3U);
    if (opcode == 0U || opcode != g_ble_probe.expected_opcode)
        return SYS_OK;

    if (opcode == HCI_READ_LOCAL_VERSION)
    {
        g_ble_probe.local_event_size = length;
        for (i = 0U; i < length && i < sizeof(g_ble_probe.local_event); ++i)
            g_ble_probe.local_event[i] = bytes[i];
    }
    else
    {
        g_ble_probe.rom_event_size = length;
        for (i = 0U; i < length && i < sizeof(g_ble_probe.rom_event); ++i)
            g_ble_probe.rom_event[i] = bytes[i];
    }

    s_reply = true;
    s_reply_result = SYS_ERR_PROTOCOL;
    if (length != (uint32_t)bytes[1] + 2U || length < 6U)
        return SYS_OK;
    g_ble_probe.last_status = bytes[5];
    if (bytes[5] != 0U)
    {
        s_reply_result = SYS_ERR_IO;
        return SYS_OK;
    }
    if (opcode == HCI_READ_LOCAL_VERSION && length == 14U)
    {
        g_ble_probe.hci_version = bytes[6];
        g_ble_probe.hci_revision = Probe_Le16(bytes + 7U);
        g_ble_probe.lmp_version = bytes[9];
        g_ble_probe.manufacturer = Probe_Le16(bytes + 10U);
        g_ble_probe.lmp_subversion = Probe_Le16(bytes + 12U);
        s_reply_result = SYS_OK;
    }
    else if (opcode == HCI_READ_ROM_VERSION && length == 7U)
    {
        g_ble_probe.rom_version = bytes[6];
        s_reply_result = SYS_OK;
    }
    return SYS_OK;
}

static uint32_t Probe_Now(void)
{
    return (uint32_t)xTaskGetTickCount() * (1000U / configTICK_RATE_HZ);
}

static SysResult_t Probe_Pump(BspBtUartDiagnostics_t *uart)
{
    uint8_t bytes[64];
    uint32_t count, i;
    SysResult_t result = SYS_OK;
    //每轮最多消费256字节，避免持续输入霸占任务
    for (i = 0U; i < 4U; ++i)
    {
        result = BSP_BT_UART_Read(bytes, sizeof(bytes), &count);
        if (result == SYS_ERR_BUSY)
        {
            result = SYS_OK;
            break;
        }
        if (result != SYS_OK)
            break;
        g_ble_probe.rx_bytes += count;
        result = BtH5_Feed(&s_link, bytes, count);
        if (result != SYS_OK)
            break;
    }
    if (result == SYS_OK)
        result = BtH5_Poll(&s_link, Probe_Now());

    (void)BSP_BT_UART_GetDiagnostics(uart);
    g_ble_probe.uart_fault = uart->fault;
    g_ble_probe.h5_state = s_link.debug.state;
    g_ble_probe.h5_attempts = s_link.debug.link_attempts;
    g_ble_probe.h5_rx_frames = s_link.debug.rx_frames;
    g_ble_probe.h5_tx_frames = s_link.debug.tx_frames;
    g_ble_probe.h5_header_errors = s_link.debug.header_errors;
    g_ble_probe.h5_framing_errors = s_link.debug.framing_errors;
    g_ble_probe.h5_crc_errors = s_link.debug.crc_errors;
    g_ble_probe.h5_retransmits = s_link.debug.retransmits;
    return result;
}

static SysResult_t Probe_Wait(BleProbeStage_t stage, BspBtUartDiagnostics_t *uart)
{
    const uint32_t start = Probe_Now();
    const uint32_t timeout = stage == BLE_PROBE_LINK ? PROBE_LINK_TIMEOUT_MS : PROBE_COMMAND_TIMEOUT_MS;
    SysResult_t result;

    do
    {
        result = Probe_Pump(uart);
        if (result != SYS_OK)
            return result;
        if (stage == BLE_PROBE_LINK && s_link.debug.state == BT_H5_ACTIVE)
            return SYS_OK;
        if (stage == BLE_PROBE_DRAIN && uart->tx_idle && !s_link.ack_pending &&
            !s_link.awaiting && s_link.encoded_offset == s_link.encoded_length)
            return SYS_OK;
        if ((stage == BLE_PROBE_LOCAL_VERSION || stage == BLE_PROBE_ROM_VERSION) && s_reply)
        {
            if (s_reply_result != SYS_OK || !s_link.awaiting)
                return s_reply_result;
        }
        vTaskDelay(1U);
    } while ((uint32_t)(Probe_Now() - start) < timeout);
    return SYS_ERR_TIMEOUT;
}

static SysResult_t Probe_Command(BleProbeStage_t stage, uint16_t opcode, BspBtUartDiagnostics_t *uart)
{
    const uint8_t command[] = {(uint8_t)opcode, (uint8_t)(opcode >> 8U), 0U};
    SysResult_t result;
    g_ble_probe.stage = stage;
    g_ble_probe.expected_opcode = opcode;
    g_ble_probe.last_status = UINT32_MAX;
    s_reply = false;
    result = BtH5_Send(&s_link, 1U, command, sizeof(command));
    return result == SYS_OK ? Probe_Wait(stage, uart) : result;
}

static void Probe_Run(void *argument)
{
    static const BtH5Ops_t ops = {Probe_Write, Probe_Receive, NULL};
    BspBtUartDiagnostics_t uart;
    SysResult_t result;
    (void)argument;

    //给模块上电留出时间，启动后只试一轮，保留失败现场
    vTaskDelay(pdMS_TO_TICKS(500U));
    g_ble_probe.stage = BLE_PROBE_LINK;
    result = BSP_BT_UART_GetDiagnostics(&uart);
    if (result == SYS_OK && !uart.ready)
        result = uart.last_result != SYS_OK ? uart.last_result : SYS_ERR_INVALID_STATE;
    if (result == SYS_OK)
        result = BtH5_Start(&s_link, &ops, Probe_Now());
    if (result == SYS_OK)
        result = Probe_Wait(BLE_PROBE_LINK, &uart);
    if (result == SYS_OK)
        result = Probe_Command(BLE_PROBE_LOCAL_VERSION, HCI_READ_LOCAL_VERSION, &uart);
    if (result == SYS_OK)
        result = Probe_Command(BLE_PROBE_ROM_VERSION, HCI_READ_ROM_VERSION, &uart);
    if (result == SYS_OK)
    {
        g_ble_probe.stage = BLE_PROBE_DRAIN;
        result = Probe_Wait(BLE_PROBE_DRAIN, &uart);
    }
    g_ble_probe.result = result;
    if (result != SYS_OK)
        g_ble_probe.failed_stage = g_ble_probe.stage;
    g_ble_probe.stage = result == SYS_OK ? BLE_PROBE_DONE : BLE_PROBE_FAILED;

    for (;;)
        vTaskSuspend(NULL);
}

static SysResult_t Probe_BootInit(void)
{
    if (xTaskCreateStatic(Probe_Run, "BleProbe", sizeof(s_stack) / sizeof(s_stack[0]),
                          NULL, 2U, s_stack, &s_task) == NULL)
    {
        g_ble_probe.stage = BLE_PROBE_FAILED;
        g_ble_probe.result = SYS_ERR_NO_RESOURCE;
    }
    return SYS_OK;
}

FRAMEWORK_INIT_REGISTER(Probe_BootInit, INIT_LEVEL_SERVICE);
