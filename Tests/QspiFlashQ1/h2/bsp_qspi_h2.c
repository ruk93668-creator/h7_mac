//仅 H2 镜像重定向三个调用。生产 BSP 和 HAL 文件按锁定版本编译。
#include "h2.h"
#define HAL_QSPI_Command H2_Command
#define HAL_QSPI_Receive H2_Receive
#define HAL_QSPI_DeInit H2_DeInit
#include "../../../CM7/BSP/bsp_qspi.c"

uint32_t H2_ControllerOwned(void)
{
    return s_owned ? 1U : 0U;
}
