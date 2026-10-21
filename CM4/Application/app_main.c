#include "app_main.h"
#include "app_oled.h"
#include "framework_init.h"

//按顺序初始化各应用模块，失败时立即返回
SysResult_t App_Init(void)
{
    return App_OledInit();
}

//统一轮询各应用模块
void App_Run(void)
{
    App_OledRun();
}

//在设备初始化完成后启动应用
FRAMEWORK_INIT_REGISTER(App_Init, INIT_LEVEL_APPLICATION);
