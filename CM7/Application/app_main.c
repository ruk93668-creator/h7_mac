#include "app_main.h"
#include "app_camera.h"
#include "framework_init.h"

//初始化CM7正式应用模块
SysResult_t App_Init(void)
{
	return App_CameraInit();
}

//统一轮询CM7正式应用模块
void App_Run(void)
{
	App_CameraRun();
}

//保留Application初始化入口
FRAMEWORK_INIT_REGISTER(
	App_Init,
	INIT_LEVEL_APPLICATION
);
