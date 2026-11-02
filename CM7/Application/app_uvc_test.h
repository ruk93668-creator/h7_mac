#ifndef CM7_APP_UVC_TEST_H
#define CM7_APP_UVC_TEST_H

#include "sys_result.h"

//仅在UVC测试模式生成一次固定彩条
SysResult_t App_UvcTestInit(void);

//上一帧归还且Class就绪后借出同一幅图像
void App_UvcTestRun(void);

#endif
