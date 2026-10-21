#ifndef CM4_APP_OLED_H
#define CM4_APP_OLED_H

#include "sys_result.h"

//获取OLED、清空帧缓冲并启动100ms周期刷新
SysResult_t App_OledInit(void);

//持续轮询OLED，推进异步刷新
void App_OledRun(void);

#endif /* CM4_APP_OLED_H */
