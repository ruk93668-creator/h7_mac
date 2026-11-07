#ifndef CM4_OLED_UI_H
#define CM4_OLED_UI_H

#include "oled_device.h"
#include "ipc_application.h"

//128列只能放下21个6像素字符，末列保留2像素
#define OLED_UI_COLUMNS 21U
#define OLED_UI_ROWS 4U

//根据完整IPC副本绘制四行监控画面，只允许在刷新IDLE时调用
SysResult_t OledUi_Render(OledDevice_t *oled, const IpcApplicationView_t *view);

#endif
