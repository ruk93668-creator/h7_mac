#include "app_main.h"
#include "app_ov5640.h"
#include "framework_init.h"

//初始化CM7应用模块
SysResult_t App_Init(void)
{
	return App_Ov5640Init();
}

//统一推进CM7应用模块
void App_Run(void)
{
	App_Ov5640Run();
}

//注册Application初始化
FRAMEWORK_INIT_REGISTER(
	App_Init,
	INIT_LEVEL_APPLICATION
);
