#ifndef CM7_APP_CAMERA_H
#define CM7_APP_CAMERA_H

#include "sys_result.h"
#include "camera_config.h"

//获取当前只读配置，供Device和后续Media共用
const CameraConfig_t *App_CameraGetConfig(void);

//配置传感器并初始化Media，只启动一次单帧采集
SysResult_t App_CameraInit(void);

//推进单帧采集、验收和释放，由App_Run持续调用
void App_CameraRun(void);

#endif
