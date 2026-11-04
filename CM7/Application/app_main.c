#include "app_main.h"
#include "framework_init.h"
#include "usb_mode.h"

//检查阶段01应用模式
SysResult_t App_Init(void)
{

    //Initcall只接受TEST准备状态，不启动Camera采集或USB连接
    return USB_Mode_GetMode() == USB_MODE_TEST ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}

FRAMEWORK_INIT_REGISTER(App_Init, INIT_LEVEL_APPLICATION);
