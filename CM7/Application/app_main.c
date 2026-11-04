#include "app_main.h"
#include "framework_init.h"
#include "usb_mode.h"

//阶段01只验收TEST控制面，不在Initcall启动采集或USB
SysResult_t App_Init(void)
{
    return USB_Mode_GetMode() == USB_MODE_TEST ? SYS_OK : SYS_ERR_NOT_SUPPORTED;
}

FRAMEWORK_INIT_REGISTER(App_Init, INIT_LEVEL_APPLICATION);
