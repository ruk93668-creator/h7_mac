#include "bsp_bt_uart.h"
#include "stm32h7xx_hal.h"
#include "framework_init.h"
#include <stddef.h>
#include <stdbool.h>

//IRQ不调用RTOS；优先级低于DCMI，与SDMMC同级
#define BT_UART_IRQ_PRIORITY 7U
#define BT_UART_COPY_LIMIT 64U
#define BT_UART_IRQ_BUDGET 32U
#define BT_UART_ACK_POLLS 100000U
#define BT_UART_ACK_MS 10U
#define BT_UART_ERRORS (USART_ISR_ORE | USART_ISR_FE | USART_ISR_PE | USART_ISR_NE)
#define BT_UART_CLEAR (USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_PECF | USART_ICR_NECF)

//固定数组无需堆；不更改其他模块的静态分配约束
static uint8_t s_rx[BSP_BT_UART_RX_BYTES];
static uint8_t s_tx[BSP_BT_UART_TX_BYTES];
static volatile uint32_t s_rx_head, s_rx_tail, s_tx_head, s_tx_tail;
static volatile bool s_owned, s_busy, s_clock_was_on;
static uint32_t s_previous_priority;
static volatile BspBtUartDiagnostics_t s_debug;

typedef struct
{
    uint32_t mode, type, speed, pull, af;
} BtUartPinSave_t;
static BtUartPinSave_t s_tx_pin, s_rx_pin;

//只修改两个授权引脚，释放时逐字段恢复，不复位整个GPIO端口
static void BtUart_SavePin(GPIO_TypeDef *port, uint32_t pin, BtUartPinSave_t *saved)
{
    saved->mode = (port->MODER >> (pin * 2U)) & 3U;
    saved->type = (port->OTYPER >> pin) & 1U;
    saved->speed = (port->OSPEEDR >> (pin * 2U)) & 3U;
    saved->pull = (port->PUPDR >> (pin * 2U)) & 3U;
    saved->af = (port->AFR[pin / 8U] >> ((pin % 8U) * 4U)) & 15U;
}

static void BtUart_RestorePin(GPIO_TypeDef *port, uint32_t pin, const BtUartPinSave_t *saved)
{
    uint32_t shift = pin * 2U;
    MODIFY_REG(port->AFR[pin / 8U], 15UL << ((pin % 8U) * 4U), saved->af << ((pin % 8U) * 4U));
    MODIFY_REG(port->OTYPER, 1UL << pin, saved->type << pin);
    MODIFY_REG(port->OSPEEDR, 3UL << shift, saved->speed << shift);
    MODIFY_REG(port->PUPDR, 3UL << shift, saved->pull << shift);
    MODIFY_REG(port->MODER, 3UL << shift, saved->mode << shift);
}

static bool BtUart_ContextValid(void)
{
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U;
}

static uint32_t BtUart_Lock(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    return saved;
}

static void BtUart_Unlock(uint32_t saved)
{
    __DMB();
    __set_PRIMASK(saved);
}

static void BtUart_Discard(void)
{
    s_debug.rx_discarded += s_rx_head - s_rx_tail;
    s_debug.tx_discarded += s_tx_head - s_tx_tail;
    s_rx_head = s_rx_tail = s_tx_head = s_tx_tail = 0U;
}

//错误后停止本模块中断，避免坏字节混入后续流及持续ORE中断风暴
static void BtUart_Fault(uint32_t flags)
{
    if ((flags & USART_ISR_ORE) != 0U) { s_debug.ore++; s_debug.fault |= BSP_BT_UART_ERROR_ORE; }
    if ((flags & USART_ISR_FE) != 0U) { s_debug.fe++; s_debug.fault |= BSP_BT_UART_ERROR_FE; }
    if ((flags & USART_ISR_PE) != 0U) { s_debug.pe++; s_debug.fault |= BSP_BT_UART_ERROR_PE; }
    if ((flags & USART_ISR_NE) != 0U) { s_debug.ne++; s_debug.fault |= BSP_BT_UART_ERROR_NE; }
    CLEAR_BIT(USART2->CR1, USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TXEIE_TXFNFIE | USART_CR1_PEIE);
    CLEAR_BIT(USART2->CR3, USART_CR3_EIE);
    USART2->ICR = BT_UART_CLEAR;
    USART2->RQR = USART_RQR_RXFRQ;
    BtUart_Discard();
    s_debug.last_result = SYS_ERR_IO;
}

//双重上界：Tick未运行或被冻结时仍能退出；等待期间不全局关中断
static SysResult_t BtUart_WaitReady(void)
{
    uint32_t start = HAL_GetTick();
    uint32_t polls;
    for (polls = 0U; polls < BT_UART_ACK_POLLS; polls++)
    {
        if ((USART2->ISR & (USART_ISR_TEACK | USART_ISR_REACK)) == (USART_ISR_TEACK | USART_ISR_REACK))
            return SYS_OK;
        if ((uint32_t)(HAL_GetTick() - start) >= BT_UART_ACK_MS)
            break;
    }
    return SYS_ERR_TIMEOUT;
}

static void BtUart_Release(void)
{
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    USART2->CR1 = 0U;
    USART2->CR3 = 0U;
    USART2->ICR = BT_UART_CLEAR;
    USART2->RQR = USART_RQR_RXFRQ;
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    NVIC_SetPriority(USART2_IRQn, s_previous_priority);
    BtUart_RestorePin(GPIOD, 5U, &s_tx_pin);
    BtUart_RestorePin(GPIOA, 3U, &s_rx_pin);
    //GPIO时钟可能被其他外设共享，始终保留；仅恢复本UART的时钟门控
    if (!s_clock_was_on) __HAL_RCC_USART2_CLK_DISABLE();
    BtUart_Discard();
    s_debug.ready = 0U;
    s_owned = false;
}

SysResult_t BSP_BT_UART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    SysResult_t result;
    uint32_t saved, hz, divider;
    if (!BtUart_ContextValid()) return SYS_ERR_INVALID_STATE;
    saved = BtUart_Lock();
    if (s_busy || s_owned) { BtUart_Unlock(saved); return SYS_ERR_BUSY; }
    s_busy = true;
    s_debug.init_attempts++;
    BtUart_Unlock(saved);

    //只读取共享USART234578时钟源，不修改选择器或PLL
    hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART2);
    divider = (hz + BSP_BT_UART_BAUD / 2U) / BSP_BT_UART_BAUD;
    result = SYS_ERR_NOT_SUPPORTED;
    if (divider < 16U || divider > 65535U) goto finish;
    //超出2%误差不宣告串口就绪
    if ((hz / divider > BSP_BT_UART_BAUD + BSP_BT_UART_BAUD / 50U) ||
        (hz / divider < BSP_BT_UART_BAUD - BSP_BT_UART_BAUD / 50U)) goto finish;

    saved = BtUart_Lock();
    s_clock_was_on = __HAL_RCC_USART2_IS_CLK_ENABLED() != 0U;
    __HAL_RCC_USART2_CLK_ENABLE();
    //拒绝接管已启用的外设/IRQ；软件无法检测CM4并发接管，依赖CM7所有权约定
    if ((USART2->CR1 & USART_CR1_UE) != 0U || NVIC_GetEnableIRQ(USART2_IRQn) != 0U)
    {
        if (!s_clock_was_on) __HAL_RCC_USART2_CLK_DISABLE();
        BtUart_Unlock(saved);
        result = SYS_ERR_BUSY;
        goto finish;
    }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    BtUart_SavePin(GPIOD, 5U, &s_tx_pin);
    BtUart_SavePin(GPIOA, 3U, &s_rx_pin);
    s_previous_priority = NVIC_GetPriority(USART2_IRQn);
    s_owned = true;
    BtUart_Discard();
    s_debug.fault = 0U;
    s_debug.kernel_hz = hz;
    s_debug.brr = divider;
    s_debug.actual_baud = hz / divider;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    gpio.Pin = GPIO_PIN_5;
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = GPIO_PIN_3;
    HAL_GPIO_Init(GPIOA, &gpio);
    USART2->CR1 = 0U;
    USART2->CR2 = 0U;//1停止位，异步，无反相
    USART2->CR3 = 0U;//无RTS/CTS，无DMA，保留ORE检测
    USART2->PRESC = 0U;
    USART2->BRR = divider;
    USART2->ICR = BT_UART_CLEAR;
    USART2->RQR = USART_RQR_RXFRQ;
    //M0=1/PCE=1得到8数据位+偶校验；FIFO关闭，字节级中断
    USART2->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    BtUart_Unlock(saved);
    result = BtUart_WaitReady();
    saved = BtUart_Lock();
    if (result == SYS_OK)
    {
        s_debug.ready = 1U;
        HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
        HAL_NVIC_SetPriority(USART2_IRQn, BT_UART_IRQ_PRIORITY, 0U);
        USART2->CR3 = USART_CR3_EIE;
        SET_BIT(USART2->CR1, USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE);
    }
    else BtUart_Release();
    BtUart_Unlock(saved);
finish:
    saved = BtUart_Lock();
    s_debug.last_result = result;
    s_busy = false;
    if (result == SYS_OK && s_debug.ready) HAL_NVIC_EnableIRQ(USART2_IRQn);
    BtUart_Unlock(saved);
    return result;
}

SysResult_t BSP_BT_UART_DeInit(void)
{
    uint32_t saved;
    if (!BtUart_ContextValid()) return SYS_ERR_INVALID_STATE;
    saved = BtUart_Lock();
    if (s_busy) { BtUart_Unlock(saved); return SYS_ERR_BUSY; }
    if (s_owned) BtUart_Release();
    s_debug.last_result = SYS_OK;
    BtUart_Unlock(saved);
    return SYS_OK;
}

SysResult_t BSP_BT_UART_Write(const uint8_t *data, uint32_t length, uint32_t *count)
{
    uint32_t saved, n = 0U, pending;
    SysResult_t result;
    if (count == NULL) return SYS_ERR_INVALID_ARG;
    *count = 0U;
    if (data == NULL && length != 0U) return SYS_ERR_INVALID_ARG;
    if (!BtUart_ContextValid()) return SYS_ERR_INVALID_STATE;
    saved = BtUart_Lock();
    if (!s_debug.ready || s_busy) result = SYS_ERR_INVALID_STATE;
    else if (s_debug.fault != 0U) result = SYS_ERR_IO;
    else
    {
        while (n < length && n < BT_UART_COPY_LIMIT && s_tx_head - s_tx_tail < BSP_BT_UART_TX_BYTES)
        {
            s_tx[s_tx_head % BSP_BT_UART_TX_BYTES] = data[n++];
            s_tx_head++;
        }
        pending = s_tx_head - s_tx_tail;
        if (pending > s_debug.tx_high_water) s_debug.tx_high_water = pending;
        if (pending != 0U) SET_BIT(USART2->CR1, USART_CR1_TXEIE_TXFNFIE);
        *count = n;
        result = n == length ? SYS_OK : SYS_ERR_BUSY;
    }
    BtUart_Unlock(saved);
    return result;
}

SysResult_t BSP_BT_UART_Read(uint8_t *data, uint32_t capacity, uint32_t *count)
{
    uint32_t saved, n = 0U;
    SysResult_t result;
    if (count == NULL) return SYS_ERR_INVALID_ARG;
    *count = 0U;
    if (data == NULL && capacity != 0U) return SYS_ERR_INVALID_ARG;
    if (!BtUart_ContextValid()) return SYS_ERR_INVALID_STATE;
    saved = BtUart_Lock();
    if (!s_debug.ready || s_busy) result = SYS_ERR_INVALID_STATE;
    else if (s_debug.fault != 0U) result = SYS_ERR_IO;
    else
    {
        while (n < capacity && n < BT_UART_COPY_LIMIT && s_rx_head != s_rx_tail)
        {
            data[n++] = s_rx[s_rx_tail % BSP_BT_UART_RX_BYTES];
            s_rx_tail++;
        }
        *count = n;
        //Read允许短读；无数据时BUSY，容量0为无操作
        result = n != 0U || capacity == 0U ? SYS_OK : SYS_ERR_BUSY;
    }
    BtUart_Unlock(saved);
    return result;
}

SysResult_t BSP_BT_UART_Recover(void)
{
    uint32_t saved;
    SysResult_t result;
    if (!BtUart_ContextValid()) return SYS_ERR_INVALID_STATE;
    saved = BtUart_Lock();
    if (s_busy) { BtUart_Unlock(saved); return SYS_ERR_BUSY; }
    if (!s_owned) { BtUart_Unlock(saved); return SYS_ERR_INVALID_STATE; }
    s_busy = true;
    s_debug.ready = 0U;
    s_debug.recover_attempts++;
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    USART2->CR1 = 0U;
    USART2->CR3 = 0U;
    BtUart_Discard();
    USART2->ICR = BT_UART_CLEAR;
    USART2->RQR = USART_RQR_RXFRQ;
    USART2->CR1 = USART_CR1_M0 | USART_CR1_PCE | USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    BtUart_Unlock(saved);
    result = BtUart_WaitReady();
    saved = BtUart_Lock();
    if (result == SYS_OK)
    {
        s_debug.fault = 0U;
        s_debug.ready = 1U;
        HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
        USART2->CR3 = USART_CR3_EIE;
        SET_BIT(USART2->CR1, USART_CR1_RXNEIE_RXFNEIE | USART_CR1_PEIE);
    }
    else BtUart_Release();
    s_debug.last_result = result;
    s_busy = false;
    if (result == SYS_OK && s_debug.ready) HAL_NVIC_EnableIRQ(USART2_IRQn);
    BtUart_Unlock(saved);
    return result;
}

SysResult_t BSP_BT_UART_GetDiagnostics(BspBtUartDiagnostics_t *diagnostics)
{
    uint32_t saved;
    if (diagnostics == NULL) return SYS_ERR_INVALID_ARG;
    if (!BtUart_ContextValid()) return SYS_ERR_INVALID_STATE;
    saved = BtUart_Lock();
    *diagnostics = s_debug;
    diagnostics->rx_pending = s_rx_head - s_rx_tail;
    diagnostics->tx_pending = s_tx_head - s_tx_tail;
    diagnostics->tx_idle = s_debug.ready && !s_busy && s_debug.fault == 0U && diagnostics->tx_pending == 0U &&
        (USART2->ISR & USART_ISR_TC) != 0U;
    BtUart_Unlock(saved);
    return SYS_OK;
}

void USART2_IRQHandler(void)
{
    uint32_t budget, flags, value, pending;
    s_debug.irq_count++;
    if (!s_debug.ready || s_busy || s_debug.fault != 0U) return;
    for (budget = 0U; budget < BT_UART_IRQ_BUDGET; budget++)
    {
        flags = USART2->ISR;
        if ((flags & BT_UART_ERRORS) != 0U) { BtUart_Fault(flags); return; }
        if ((flags & USART_ISR_RXNE_RXFNE) == 0U) break;
        value = USART2->RDR;
        if (s_rx_head - s_rx_tail == BSP_BT_UART_RX_BYTES)
        {
            s_debug.overflow++;
            s_debug.fault |= BSP_BT_UART_ERROR_OVERFLOW;
            BtUart_Fault(0U);
            return;
        }
        s_rx[s_rx_head % BSP_BT_UART_RX_BYTES] = (uint8_t)value;
        s_rx_head++;
        s_debug.rx_bytes++;
        pending = s_rx_head - s_rx_tail;
        if (pending > s_debug.rx_high_water) s_debug.rx_high_water = pending;
    }
    //每次IRQ最多发送一字节，避免TX饿死接收及其他中断
    if ((USART2->CR1 & USART_CR1_TXEIE_TXFNFIE) != 0U && (USART2->ISR & USART_ISR_TXE_TXFNF) != 0U)
    {
        if (s_tx_head != s_tx_tail)
        {
            USART2->TDR = s_tx[s_tx_tail % BSP_BT_UART_TX_BYTES];
            s_tx_tail++;
            s_debug.tx_bytes++;
        }
        if (s_tx_head == s_tx_tail) CLEAR_BIT(USART2->CR1, USART_CR1_TXEIE_TXFNFIE);
    }
}

//可选外设失败只记录自身诊断，不阻断既有Camera/USB/Storage启动
//本注册仅初始化UART，不发HCI、不创建BLE任务，也不重置模块
static SysResult_t BtUart_BootInit(void)
{
    (void)BSP_BT_UART_Init();
    return SYS_OK;
}
FRAMEWORK_INIT_REGISTER(BtUart_BootInit, INIT_LEVEL_BUS);
