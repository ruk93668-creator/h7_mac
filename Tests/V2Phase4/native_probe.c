#include "../../CM7/ApplicationV2/Modules/Camera/camera_format.h"
#include <string.h>
uint32_t g_failure;
int Native_Run(uint32_t i){CameraConfig_t n;CoreMediaFormat f={1,3,1,160,120,320,38400,38400,1,1,1,32,32,0};g_failure=0;
 if(i==1)f.width=320,f.height=240,f.bytes_per_line=640,f.size_image=f.max_payload=153600;
 if(VCamera_Translate(&f,&n)!=(i==1?SYS_ERR_NOT_SUPPORTED:SYS_OK)){g_failure=__LINE__;return 1;}return 0;}
