#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include "camera_config.h"
#include "sys_result.h"
#include <stdbool.h>

//初始化图像采集模块
SysResult_t CameraCapture_Init(const CameraConfig_t *config);

//启动一次图像采集
SysResult_t CameraCapture_Start(void);

//处理采集完成、错误和超时
void CameraCapture_Process(void);

//获取完整图像，成功时填写frame
bool CameraCapture_GetFrame(CameraFrame_t *frame);

//释放当前完整帧，允许下一次采集
SysResult_t CameraCapture_ReleaseFrame(void);

//停止当前采集，必要时恢复底层硬件
SysResult_t CameraCapture_Stop(void);

//查询最近一次采集结果
SysResult_t CameraCapture_GetLastResult(void);
#endif
