#ifndef CM7_CAMERA_MJPEG_FIFO_H
#define CM7_CAMERA_MJPEG_FIFO_H

#include "camera_config.h"

//独立JPEG字节FIFO，地址结束值均不包含自身
#define CAMERA_MJPEG_FIFO_BASE       0xD0480000UL
//连续JPEG字节环的总容量，单位字节
#define CAMERA_MJPEG_FIFO_BYTES      (512UL * 1024UL)
//每次DMA轮换使用的物理块大小，单位字节
#define CAMERA_MJPEG_BLOCK_BYTES     (8UL * 1024UL)
//字节环内可纳入DMA轮换的物理块数量
#define CAMERA_MJPEG_BLOCK_COUNT     64U
//两个无法安全入环时使用的丢弃缓冲起始地址
#define CAMERA_MJPEG_DROP_BASE       (CAMERA_MJPEG_FIFO_BASE + CAMERA_MJPEG_FIFO_BYTES)
//字节环与丢弃缓冲的排他结束地址
#define CAMERA_MJPEG_REGION_END      (CAMERA_MJPEG_DROP_BASE + 2UL * CAMERA_MJPEG_BLOCK_BYTES)
//完整JPEG描述符队列的静态容量
#define CAMERA_MJPEG_FRAME_QUEUE      7U

//JPEG物理帧的租约表上限
#define CAMERA_MJPEG_MAX_LOANS 7U

//跨任务只传递凭据，Media的释放始终由CameraTask执行
typedef struct
{
    //当前JPEG采集会话号，重启后不能用旧会话归还
    uint32_t session;
    //在对应采集会话内分配的租约编号
    uint32_t id;
    //物理租约的登记消费者，归还时与原记录核对
    CameraFrameConsumer_t consumer;
} CameraMjpegLease_t;

// 每个消费者最多持有一个显式 JPEG 租约，由 CameraTask 维护所有权。
SysResult_t CameraMjpegFifo_Acquire(CameraFrameConsumer_t consumer,
    CameraMjpegLease_t *lease, CameraFrame_t *frame);
//CameraTask核对会话、编号和消费者后归还显式租约
SysResult_t CameraMjpegFifo_Release(const CameraMjpegLease_t *lease);

// 兼容原有视图持有接口，其凭据只能通过 ReleaseLease 归还。
SysResult_t CameraMjpegFifo_RetainFrame(const CameraFrame_t *frame, CameraMjpegLease_t *lease);
//归还Legacy兼容保留引用，最后一个拥有者才释放物理块
SysResult_t CameraMjpegFifo_ReleaseLease(const CameraMjpegLease_t *lease);

//Camera任务启动JPEG连续字节流，不占用原YUY2七缓冲
SysResult_t CameraMjpegFifo_Start(const CameraConfig_t *config);

//Camera任务解析已完成DMA块，不读取正在写入的目标
SysResult_t CameraMjpegFifo_Process(void);

//Camera任务借出一张帧描述，数据可能跨FIFO末尾分成两段
bool CameraMjpegFifo_GetFrame(CameraFrame_t *frame);

//Camera任务归还唯一借出帧，USB确认释放后才允许调用
SysResult_t CameraMjpegFifo_ReleaseFrame(void);

//查询是否仍有外部借用，不能据此跳过底层Stop
bool CameraMjpegFifo_HasLoan(void);
//短临界区内读取物理块及活动租约数量
void CameraMjpegFifo_ReadOccupancy(uint32_t *blocks, uint32_t *loans);

//只在BSP Stop成功后清理接收端，仍保留外部借用
void CameraMjpegFifo_OnStopped(void);

//读取有效帧及已丢弃帧统计，不把丢失字节块冒充帧数
void CameraMjpegFifo_GetStats(uint32_t *completed, uint32_t *dropped);

#endif
