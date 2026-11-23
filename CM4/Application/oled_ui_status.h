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

#endif
