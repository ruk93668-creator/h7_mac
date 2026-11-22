#ifndef CM7_BSP_BT_UART_H
#define CM7_BSP_BT_UART_H

#include "sys_result.h"
#include <stdint.h>

#define BSP_BT_UART_RX_BYTES 2048U
#define BSP_BT_UART_TX_BYTES 1024U
#define BSP_BT_UART_BAUD 115200U

//错误位锁存到显式恢复；统计值按观察到的事件计数，不代表丢失字节总数
#define BSP_BT_UART_ERROR_ORE      1U
#define BSP_BT_UART_ERROR_FE       2U
#define BSP_BT_UART_ERROR_PE       4U
#define BSP_BT_UART_ERROR_NE       8U
#define BSP_BT_UART_ERROR_OVERFLOW 16U

typedef struct
{
    uint32_t ready;
    uint32_t fault;
    uint32_t kernel_hz;
    uint32_t actual_baud;
    uint32_t brr;
    uint32_t irq_count;
    uint32_t rx_bytes;
    uint32_t tx_bytes;//已写TDR，不等于最后停止位发送完成
    uint32_t ore;
    uint32_t fe;
    uint32_t pe;
    uint32_t ne;
    uint32_t overflow;
    uint32_t rx_discarded;//仅已知被清除的软件缓冲字节
    uint32_t tx_discarded;
    uint32_t rx_pending;
    uint32_t tx_pending;
    uint32_t rx_high_water;
    uint32_t tx_high_water;
    uint32_t tx_idle;//仅ready且软件队列空、硬件TC置位时为1
    uint32_t init_attempts;
    uint32_t recover_attempts;
    SysResult_t last_result;
} BspBtUartDiagnostics_t;

//CM7独占USART2，PD5/PA3 AF7，115200 8E1，无DMA/硬件流控
//所有API仅线程/启动上下文调用；不接受ISR或全局关中断上下文
SysResult_t BSP_BT_UART_Init(void);
//有界强制停止，丢弃未完成TX；正常关闭前先检查tx_idle
SysResult_t BSP_BT_UART_DeInit(void);
//非阻塞，单次最多复制64字节；返回BUSY可伴随部分进展，必须检查count
//Write复制成功后调用者可立即复用输入；完成入队不代表线上已发送
SysResult_t BSP_BT_UART_Write(const uint8_t *data, uint32_t length, uint32_t *count);
SysResult_t BSP_BT_UART_Read(uint8_t *data, uint32_t capacity, uint32_t *count);
//清除收发缓存和UART错误，重新使能并等待ACK；保留累计统计，不复位蓝牙模块
SysResult_t BSP_BT_UART_Recover(void);
SysResult_t BSP_BT_UART_GetDiagnostics(BspBtUartDiagnostics_t *diagnostics);

#endif
