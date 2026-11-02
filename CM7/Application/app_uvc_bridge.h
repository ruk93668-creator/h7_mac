#ifndef CM7_APP_UVC_BRIDGE_H
#define CM7_APP_UVC_BRIDGE_H

#include "sys_result.h"

//CM7正常主循环持续调用，借用Media完整帧交给UVC，确认归还后再释放
void App_UvcBridgeProcess(void);

#endif
