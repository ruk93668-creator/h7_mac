#include "app_main.h"
#include "framework_init.h"
#include "usb_mode.h"

//检查原生运行时应用模式
SysResult_t App_Init(void)
{

    //Initcall只校验准备结果，业务启停由Control命令协调
    UsbMode_t mode = USB_Mode_GetMode();
    return mode == USB_MODE_TEST || mode == USB_MODE_UVC || mode == USB_MODE_NONE ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}

FRAMEWORK_INIT_REGISTER(App_Init, INIT_LEVEL_APPLICATION);
