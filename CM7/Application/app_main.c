#include "app_main.h"
#include "framework_init.h"

#include "app_camera.h"
#include "app_uvc_bridge.h"

//初始化CM7正式应用模块
SysResult_t App_Init(void)
{
	return App_CameraInitStream();
}

//统一轮询CM7正式应用模块
void App_Run(void)
{
	App_CameraRun();
	
	App_UvcBridgeProcess();
}

//保留Application初始化入口
FRAMEWORK_INIT_REGISTER(
	App_Init,
	INIT_LEVEL_APPLICATION
);
