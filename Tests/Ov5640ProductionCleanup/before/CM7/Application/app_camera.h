#ifndef CM7_APP_CAMERA_H
#define CM7_APP_CAMERA_H

#include "ov5640_device.h"

//保存一次启动配置结果，供调试器查看
typedef struct
{
	SysResult_t result;
	bool done;
	Ov5640ConfigReport_t sensor;
} AppCameraReport_t;

extern volatile AppCameraReport_t g_camera_report;

SysResult_t App_CameraInit(void);

#endif
