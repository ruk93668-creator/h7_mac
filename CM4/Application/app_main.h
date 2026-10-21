#ifndef CM4_APP_MAIN_H
#define CM4_APP_MAIN_H

#include "sys_result.h"

//初始化当前核心的应用模块
SysResult_t App_Init(void);

//轮询当前核心的应用模块，由主循环持续调用
void App_Run(void);

#endif /* CM4_APP_MAIN_H */
