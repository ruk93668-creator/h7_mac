#ifndef CM7_APP_CAMERA_H
#define CM7_APP_CAMERA_H

#include "sys_result.h"
#include "camera_config.h"

//获取当前只读配置，供Device和后续Media共用
const CameraConfig_t *App_CameraGetConfig(void);

//启动摄像头配置
SysResult_t App_CameraInit(void);

#endif
