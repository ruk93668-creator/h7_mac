#include "app_main.h"
#include "app_camera.h"
#include "app_uvc_test.h"
#include "usb_mode.h"
#include "framework_init.h"

//初始化CM7正式应用模块
SysResult_t App_Init(void)
{
    //静态UVC实验独立运行，不启动摄像头应用
    if (USB_Mode_GetMode() == USB_MODE_UVC)
        return App_UvcTestInit();

    //默认TEST保留原有相机应用
    return App_CameraInit();
}

//统一轮询CM7正式应用模块
void App_Run(void)
{
    //UVC实验只轮询静态图像，不调用采集应用
    if (USB_Mode_GetMode() == USB_MODE_UVC)
    {
        App_UvcTestRun();
        return;
    }

    App_CameraRun();
}

//保留Application初始化入口
FRAMEWORK_INIT_REGISTER(
	App_Init,
	INIT_LEVEL_APPLICATION
);
