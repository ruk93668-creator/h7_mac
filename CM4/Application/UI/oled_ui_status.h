#ifndef CM4_OLED_UI_STATUS_H
#define CM4_OLED_UI_STATUS_H

#include "oled_ui_page.h"
#include "ipc_runtime.h"
#include <stdbool.h>

//每行状态文本容量，包含NUL
#define OLED_UI_STATUS_LINE_BYTES 32U

typedef struct
{
    uint32_t completed; //整帧刷新累计次数
    uint32_t io_errors; //刷新I/O失败总数
    uint32_t render_errors; //绘制失败累计次数
    uint32_t peer_heartbeat; //本地IPC Service最近观察到的CM7心跳
    bool heartbeat_valid; //仅在线且观察到心跳进展时有效
} OledUiStatusCounters_t;

typedef struct
{
    char lines[3][OLED_UI_STATUS_LINE_BYTES]; //预格式化文本
    uint8_t first_row; //画面中显示文本的首行
} OledUiStatus_t;

//输入是一次取样得到的本地副本。未适用时返回false，不访问设备。
bool OledUi_StatusBuild(OledUiPageId_t page, const IpcApplicationView_t *view,
    const OledUiStatusCounters_t *counters, OledUiStatus_t *status);

//诊断模型最多容纳的任务条目数
#define OLED_UI_TASK_CAPACITY 8U
//本地诊断模型接受的版本号
#define OLED_UI_SYSTEM_VERSION 1U
//本地接收诊断后允许显示的时间，单位ms
#define OLED_UI_SYSTEM_MAX_AGE_MS 2000U
//有效位，任务列表可用
#define OLED_UI_SYSTEM_TASKS  1U
//有效位，FreeRTOS堆统计可用
#define OLED_UI_SYSTEM_HEAP   2U
//有效位，CPU占比可用
#define OLED_UI_SYSTEM_CPU    4U
//有效位，运行时长可用
#define OLED_UI_SYSTEM_UPTIME 8U
//有效位，内存分配失败计数可用
#define OLED_UI_SYSTEM_ALLOC_FAILURES 16U
//有效位，任务栈容量与水位可用
#define OLED_UI_TASK_STACK 1U
//有效位，任务运行占比可用
#define OLED_UI_TASK_CPU   2U

typedef struct
{
    uint32_t id; //会话内任务标识
    uint32_t stack_bytes; //真实分配栈容量，单位字节
    uint32_t hwm_bytes; //历史最小剩余栈，单位字节
    uint16_t cpu_permille; //有效窗口内运行占比，单位千分数
    uint8_t priority; //任务当前优先级
    uint8_t state; //任务当前状态
    uint8_t flags; //统计字段有效位
    char name[16]; //有界任务名
} OledUiTaskStatus_t;

//本地模型，不能映射成共享IPC结构。received_ms来自CM4本地时钟。
typedef struct
{
    uint32_t version; //诊断模型版本
    uint32_t session; //发布者会话
    uint32_t generation; //快照序号
    uint32_t received_ms; //CM4本地接收时间，单位ms
    uint32_t flags; //统计字段有效位
    uint32_t total; //真实任务总数
    uint32_t heap_total; //FreeRTOS堆容量，单位字节
    uint32_t heap_free; //当前空闲堆字节数
    uint32_t heap_min; //历史最小空闲堆字节数
    uint32_t alloc_failures; //内存分配失败次数
    uint32_t uptime_ms; //运行时间，单位ms
    uint16_t cpu_permille; //有效窗口内运行占比，单位千分数
    uint8_t count; //实际保存的任务条目数
    OledUiTaskStatus_t tasks[OLED_UI_TASK_CAPACITY]; //固定容量任务列表
} OledUiSystemStatus_t;

typedef struct
{
    uint32_t session; //发布者会话
    uint32_t id; //会话内任务标识
    uint8_t index; //当前任务索引
    bool stack_detail; //是否显示栈详情
} OledUiTaskCursor_t;

//按会话和任务ID恢复选中项并限制索引
uint8_t OledUi_SystemTaskSelect(const OledUiSystemStatus_t *system, OledUiTaskCursor_t *cursor,
    int32_t direction);
//根据会话、时效和有效位组织系统诊断文本
bool OledUi_SystemBuild(OledUiPageId_t page, const IpcApplicationView_t *ipc,
    const OledUiSystemStatus_t *system, const OledUiTaskCursor_t *cursor,
    uint32_t now, OledUiStatus_t *status);

#endif
