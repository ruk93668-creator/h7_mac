#ifndef CM7_APP_UVC_BRIDGE_H
#define CM7_APP_UVC_BRIDGE_H

#include "sys_result.h"
#include "camera_config.h"

//冷启动配置期望帧参数，持帧期间不能重新配置
SysResult_t App_UvcBridgeConfigure(const CameraConfig_t *config);

//CM7正常主循环持续调用，借用Media完整帧交给UVC，确认归还后再释放
void App_UvcBridgeProcess(void);

#endif
