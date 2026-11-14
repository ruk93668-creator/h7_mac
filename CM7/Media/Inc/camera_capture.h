#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include "camera_config.h"
#include "sys_result.h"
#include <stdbool.h>

//采集提示，不携带帧所有权，回调沿用调用者上下文
#define CAMERA_CAPTURE_EVENT_PROGRESS (1UL << 0)
#define CAMERA_CAPTURE_EVENT_ERROR    (1UL << 1)
typedef void (*CameraCaptureEventCallback_t)(uint32_t events, void *context);

typedef struct
{
    uint32_t completed_frames;
    uint32_t dropped_frames;
} CameraCaptureStats_t;

//绑定静态采集通知接收端
SysResult_t CameraCapture_SetEventCallback(CameraCaptureEventCallback_t callback, void *context);

//取得完整帧和丢帧计数快照
SysResult_t CameraCapture_GetStats(CameraCaptureStats_t *stats);

//启动前校验格式及对应接收范围，不修改采集状态
SysResult_t CameraCapture_ValidateStreamConfig(const CameraConfig_t *config);

//停止事实只由Media所有者读取，不代表帧租约已经归还
#define CAMERA_STOP_INITIALIZED 1U
#define CAMERA_STOP_ERROR       2U
#define CAMERA_STOP_DRAINING    4U
#define CAMERA_STOP_COMPLETE    8U
#define CAMERA_STOP_NORMAL_ATTEMPT     16U
#define CAMERA_STOP_PROTECTIVE_ATTEMPT 32U
typedef struct
{
    uint32_t attempt_sequence;//实际BSP停止调用序号，只用于同轮去重
    SysResult_t hardware_result;//最近实际调用结果，不随后续查询改写
    uint32_t flags;//当前状态与最近一次普通或保护性硬件停止事实
} CameraCaptureStopFacts_t;

//读取停止事实，不推进硬件或清理缓冲
SysResult_t CameraCapture_GetStopFacts(CameraCaptureStopFacts_t *facts);

//初始化图像采集模块
SysResult_t CameraCapture_Init(const CameraConfig_t *config);

//启动一次图像采集
SysResult_t CameraCapture_Start(void);

//获取完整图像，成功时填写frame
bool CameraCapture_GetFrame(CameraFrame_t *frame);

//释放当前完整帧，允许下一次采集
SysResult_t CameraCapture_ReleaseFrame(void);

//取得连续采集的完整图像，成功时借用数据并填写frame
//JPEG跨FIFO尾部时data2非空，size_bytes为总长度，size_bytes2为第二段长度
//唯一消费者归还前，两段内存都不能被DMA复用
bool CameraCapture_StreamGetFrame(CameraFrame_t *frame);

//释放当前完整帧，允许下一次采集
SysResult_t CameraCapture_StreamReleaseFrame(void);

//启动连续采集，固定格式使用五缓冲，JPEG使用独立字节FIFO
SysResult_t CameraCapture_StartStream(void);

//停止当前采集，必要时恢复底层硬件
SysResult_t CameraCapture_Stop(void);

//确认硬件已停止且所有帧引用已释放，DRAINING时返回false
bool CameraCapture_IsStopped(void);

//查询最近一次采集结果
SysResult_t CameraCapture_GetLastResult(void);

//处理采集完成、错误和超时
void CameraCapture_Process(void);

#endif
