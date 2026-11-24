#ifndef CM7_CAMERA_MJPEG_FIFO_H
#define CM7_CAMERA_MJPEG_FIFO_H

#include "camera_config.h"

//独立JPEG字节FIFO，地址结束值均不包含自身
#define CAMERA_MJPEG_FIFO_BASE       0xD0480000UL
#define CAMERA_MJPEG_FIFO_BYTES      (512UL * 1024UL)
#define CAMERA_MJPEG_BLOCK_BYTES     (8UL * 1024UL)
#define CAMERA_MJPEG_BLOCK_COUNT     64U
#define CAMERA_MJPEG_DROP_BASE       (CAMERA_MJPEG_FIFO_BASE + CAMERA_MJPEG_FIFO_BYTES)
#define CAMERA_MJPEG_REGION_END      (CAMERA_MJPEG_DROP_BASE + 2UL * CAMERA_MJPEG_BLOCK_BYTES)
#define CAMERA_MJPEG_FRAME_QUEUE      7U

#define CAMERA_MJPEG_MAX_LOANS 7U

//跨任务只传递凭据，Media的释放始终由CameraTask执行
typedef struct
{
    uint32_t session;
    uint32_t id;
    CameraFrameConsumer_t consumer;
} CameraMjpegLease_t;

// 每个消费者最多持有一个显式 JPEG 租约，由 CameraTask 维护所有权。
SysResult_t CameraMjpegFifo_Acquire(CameraFrameConsumer_t consumer,
    CameraMjpegLease_t *lease, CameraFrame_t *frame);
SysResult_t CameraMjpegFifo_Release(const CameraMjpegLease_t *lease);

// 兼容原有视图持有接口，其凭据只能通过 ReleaseLease 归还。
SysResult_t CameraMjpegFifo_RetainFrame(const CameraFrame_t *frame, CameraMjpegLease_t *lease);
SysResult_t CameraMjpegFifo_ReleaseLease(const CameraMjpegLease_t *lease);

//Camera任务启动JPEG连续字节流，不占用原YUY2五缓冲
SysResult_t CameraMjpegFifo_Start(const CameraConfig_t *config);

//Camera任务解析已完成DMA块，不读取正在写入的目标
SysResult_t CameraMjpegFifo_Process(void);

//Camera任务借出一张帧描述，数据可能跨FIFO末尾分成两段
bool CameraMjpegFifo_GetFrame(CameraFrame_t *frame);

//Camera任务归还唯一借出帧，USB确认释放后才允许调用
SysResult_t CameraMjpegFifo_ReleaseFrame(void);

//查询是否仍有外部借用，不能据此跳过底层Stop
bool CameraMjpegFifo_HasLoan(void);
#if APP_V3_JOINT_BOARD_TEST
void CameraMjpegFifo_ReadOccupancy(uint32_t *blocks, uint32_t *loans);
#endif

//只在BSP Stop成功后清理接收端，仍保留外部借用
void CameraMjpegFifo_OnStopped(void);

//读取有效帧及已丢弃帧统计，不把丢失字节块冒充帧数
void CameraMjpegFifo_GetStats(uint32_t *completed, uint32_t *dropped);

#endif
