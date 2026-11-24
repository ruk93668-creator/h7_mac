#ifndef CM4_OLED_UI_STATUS_H
#define CM4_OLED_UI_STATUS_H

#include "oled_ui_page.h"
#include "ipc_runtime.h"
#include <stdbool.h>

#define OLED_UI_STATUS_LINE_BYTES 32U

typedef struct
{
    uint32_t completed;
    uint32_t io_errors;
    uint32_t render_errors;
} OledUiStatusCounters_t;

typedef struct
{
    char lines[3][OLED_UI_STATUS_LINE_BYTES];
    uint8_t first_row;
} OledUiStatus_t;

//输入是一次取样得到的本地副本。未适用时返回false，不访问设备。
bool OledUi_StatusBuild(OledUiPageId_t page, const IpcApplicationView_t *view,
    const OledUiStatusCounters_t *counters, OledUiStatus_t *status);

#define OLED_UI_TASK_CAPACITY 8U
#define OLED_UI_SYSTEM_VERSION 1U
#define OLED_UI_SYSTEM_MAX_AGE_MS 2000U
#define OLED_UI_SYSTEM_TASKS  1U
#define OLED_UI_SYSTEM_HEAP   2U
#define OLED_UI_SYSTEM_CPU    4U
#define OLED_UI_SYSTEM_UPTIME 8U
#define OLED_UI_SYSTEM_ALLOC_FAILURES 16U
#define OLED_UI_TASK_STACK 1U
#define OLED_UI_TASK_CPU   2U

typedef struct
{
    uint32_t id;
    uint32_t stack_bytes;
    uint32_t hwm_bytes;
    uint16_t cpu_permille;
    uint8_t priority;
    uint8_t state;
    uint8_t flags;
    char name[16];
} OledUiTaskStatus_t;

//本地模型，不能映射成共享IPC结构。received_ms来自CM4本地时钟。
typedef struct
{
    uint32_t version, session, generation, received_ms, flags, total;
    uint32_t heap_total, heap_free, heap_min, alloc_failures, uptime_ms;
    uint16_t cpu_permille;
    uint8_t count;
    OledUiTaskStatus_t tasks[OLED_UI_TASK_CAPACITY];
} OledUiSystemStatus_t;

typedef struct
{
    uint32_t session;
    uint32_t id;
    uint8_t index;
    bool stack_detail;
} OledUiTaskCursor_t;

uint8_t OledUi_SystemTaskSelect(const OledUiSystemStatus_t *system, OledUiTaskCursor_t *cursor,
    int32_t direction);
bool OledUi_SystemBuild(OledUiPageId_t page, const IpcApplicationView_t *ipc,
    const OledUiSystemStatus_t *system, const OledUiTaskCursor_t *cursor,
    uint32_t now, OledUiStatus_t *status);

#endif
