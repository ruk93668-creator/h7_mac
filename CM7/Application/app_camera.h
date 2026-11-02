#ifndef CM7_APP_CAMERA_H
#define CM7_APP_CAMERA_H

#include "sys_result.h"
#include "camera_config.h"

//获取当前只读配置，供Device与Media共用
const CameraConfig_t *App_CameraGetConfig(void);

//主循环初始化并启动单帧采集，重复或交叉启动由Media拒绝
SysResult_t App_CameraInit(void);

//主循环初始化并启动连续采集，停止并释放占用后才允许切换模式
SysResult_t App_CameraInitStream(void);

//推进Media采集流程，由App_Run持续调用
void App_CameraRun(void);

#endif
