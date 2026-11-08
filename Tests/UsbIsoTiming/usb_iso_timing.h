#ifndef USB_ISO_TIMING_H
#define USB_ISO_TIMING_H

#include <stdint.h>

//默认不编译采样，当前诊断工程单独开启
#ifndef APP_USB_ISO_TIMING_TEST_ENABLED
#define APP_USB_ISO_TIMING_TEST_ENABLED 0
#endif

#define USB_ISO_TIMING_CAPACITY 512U
#define USB_ISO_TIMING_BASE 0x24020000UL

#define USB_ISO_TIME_DATAIN 1U
#define USB_ISO_TIME_CONFIRM 2U
#define USB_ISO_TIME_SUBMIT 3U
#define USB_ISO_TIME_HAL_ENTER 4U
#define USB_ISO_TIME_HAL_EXIT 5U
#define USB_ISO_TIME_RESULT 6U
#define USB_ISO_TIME_FRAME 7U
#define USB_ISO_TIME_NOTIFY_BEGIN 8U
#define USB_ISO_TIME_NOTIFY_END 9U
#define USB_ISO_TIME_WAIT_RETURN 10U
#define USB_ISO_TIME_PROCESS_ENTER 11U
#define USB_ISO_TIME_UVC_ENTER 12U
#define USB_ISO_TIME_SOF 13U
#define USB_ISO_TIME_WAIT_BEGIN 14U
#define USB_ISO_TIME_UVC_RETURN 15U
#define USB_ISO_TIME_BRIDGE_RETURN 16U
#define USB_ISO_TIME_PUBLISH_BEGIN 17U
#define USB_ISO_TIME_PUBLISH_END 18U
#define USB_ISO_TIME_STACK_BEGIN 19U
#define USB_ISO_TIME_STACK_END 20U

#define USB_ISO_TRACE_IDLE 0U
#define USB_ISO_TRACE_ACTIVE 1U
#define USB_ISO_TRACE_FROZEN 2U
#define USB_ISO_TRACE_READY 3U
#define USB_ISO_TRACE_ERROR 4U

typedef struct
{
    uint32_t cycles;//DWT周期，差值按无符号回绕计算
    uint32_t frame;//硬件USB帧号，不是软件SOF计数
    uint32_t event;//事件编号，最后写入表示本条完整
    uint32_t sequence;//EP1事务序号，BSP层为0
    uint32_t detail;//包长、返回值或完成帧数
    uint32_t diepctl;//EP1使能及目标帧控制位
    uint32_t dieptsiz;//EP1剩余传输量
    uint32_t fifo_words;//EP1 TX FIFO可用Word数
    uint32_t systick_val;//观察与RTOS Tick的相位，不修改Tick
    uint32_t ipsr;//0为任务，非0为异常上下文
    uint32_t wall_us;//现有TIM6与HAL Tick组合的微秒墙钟
    uint32_t time_flags;//位0有效，位1存在待处理更新，位2快照冲突，位3配置无效
} UsbIsoTimingRecord_t;

typedef struct
{
    uint32_t enabled;//周期计数器准备成功为1
    uint32_t request;//Watch置1采样，置2提前停止
    uint32_t state;//0等待、1采样、2冻结待收尾、3可导出、4不可用
    uint32_t count;//已预留记录数，READY后均已完成
    uint32_t capacity;//固定512，Watch修改不会扩大数组
    uint32_t record_bytes;//每条48B
    uint32_t buffer_address;//独立AXI记录区地址
    uint32_t buffer_bytes;//整个记录区长度
    uint32_t cpu_hz;//DWT周期换算依据
    uint32_t writers;//正在写入记录的上下文数量
    uint32_t rejected_requests;//采样中重启等无效请求次数
    uint32_t start_cycles;//开始周期
    uint32_t end_cycles;//冻结周期
    uint32_t stop_reason;//1记录满、2手动停止
    uint32_t magic;//0x5554494D，核对固件
    uint32_t version;//当前记录格式版本2
} UsbIsoTimingDebug_t;

//保存在DTCM，避免Watch控制被AXI的DCache副本遮挡
extern volatile UsbIsoTimingDebug_t g_usb_iso_timing;

//准备诊断与DWT，不复位其他模块使用的周期计数器
void UsbIsoTiming_Init(void);

//仅UsbTask调用，处理手动采样及冻结后的Cache清理
void UsbIsoTiming_Process(void);

//在任务或IRQ中记录有限快照，不打印、不等待、不调用USB传输
void UsbIsoTiming_Record(uint32_t event, uint32_t sequence, uint32_t detail);

#endif
