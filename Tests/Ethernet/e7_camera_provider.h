#ifndef E7_CAMERA_PROVIDER_H
#define E7_CAMERA_PROVIDER_H

#include "camera_capture.h"
#include "eth_mjpeg.h"

#define E7_JPEG_CAPACITY (60U * 1024U)
#define E7_JPEG_COPY_SEGMENT_BYTES 8U

//统一传感器与帧提供器使用的JPEG尺寸
#ifdef ETH_E7_CAMERA_QVGA
#define E7_CAMERA_WIDTH 320U
#define E7_CAMERA_HEIGHT 240U
#else
#define E7_CAMERA_WIDTH 160U
#define E7_CAMERA_HEIGHT 120U
#endif

typedef struct
{
    //CPU私有JPEG副本，发送层持有租约期间不得覆盖
    uint8_t *buffer;
    //相机DMA使用的原始JPEG FIFO
    const uint8_t *fifo;
    //分段对照时等待上一小段访存完成
    void (*copy_barrier)(void);
    uint32_t fifo_size;
    //私有帧租约序号
    uint32_t lease;
    //已校验并发布的私有JPEG帧数
    uint32_t copies;
    uint32_t invalid;
    uint32_t oversize;
    uint32_t no_frame;
    uint32_t release_errors;
    uint32_t stale_releases;
    uint32_t capture_errors;
    //相机采集帧率乘以1000
    uint32_t capture_fps_milli;
    //私有副本发布帧率乘以1000
    uint32_t copy_fps_milli;
    uint32_t last_sample;
    uint32_t last_completed;
    uint32_t last_copies;
    CameraCaptureStats_t capture;
    CameraCaptureStopFacts_t stop_facts;
    SysResult_t media_result;
    SysResult_t stop_result;
    bool running;
    bool pinned;
    //持有原始帧时禁止复用该FIFO区域
    bool original_loan;
} E7CameraProvider_t;

//借用原始JPEG，完成复制和CRC后归还原帧，再发布私有帧租约
EthMjpegAcquire_t E7Camera_Acquire(void *context, EthMjpegFrame_t *frame, uint32_t *lease);

//按租约归还私有帧，过期或重复租约只计数
void E7Camera_Release(void *context, uint32_t lease);

//确认硬件已停止、帧租约已归还后启动采集
SysResult_t E7Camera_Start(E7CameraProvider_t *provider);

//请求停止采集，并检查实际硬件停止结果
SysResult_t E7Camera_Stop(E7CameraProvider_t *provider);

//处理采集事件和错误，周期更新计数快照
void E7Camera_Poll(E7CameraProvider_t *provider, uint32_t now);

#endif
