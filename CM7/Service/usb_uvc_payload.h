#ifndef CM7_USB_UVC_PAYLOAD_H
#define CM7_USB_UVC_PAYLOAD_H

#include "sys_result.h"
#include "video_frame_view.h"
#include <stdbool.h>
#include <stdint.h>

//长度均包含2字节Header，不包含USB协议开销
#define USB_UVC_PAYLOAD_HEADER_SIZE 2U
#define USB_UVC_PAYLOAD_MAX_PACKET_SIZE 1023U

//内部存储向上对齐，供32位PIO安全读取
#define USB_UVC_PAYLOAD_STORAGE_SIZE \
    ((USB_UVC_PAYLOAD_MAX_PACKET_SIZE + 3U) & ~3U)

//纯软件分包器，不注册USB、不操作端点、不知道Camera或Media
//同一对象必须串行调用，不同对象互不影响；Class负责USB IRQ串行化
typedef enum
{
    USB_UVC_PAYLOAD_UNINITIALIZED = 0,
    USB_UVC_PAYLOAD_IDLE,
    USB_UVC_PAYLOAD_FRAME,
    USB_UVC_PAYLOAD_PACKET,
    USB_UVC_PAYLOAD_CANCELLING
} UsbUvcPayloadState_t;

//只读数据包视图，Token用于匹配对应的异步完成通知
typedef struct
{
    const uint8_t *data;
    uint16_t length;
    uint32_t token;
} UsbUvcPayloadPacket_t;

//静态分配的独立对象，首次使用前必须以{0}初始化
//成员由模块维护，调用方不得修改或复制活动对象
//外部图像和内部包缓冲区需保留到完成或取消确认
//本模块不操作硬件，完成及取消确认由调用方保证真实有效
typedef struct
{
    const uint8_t *frame;
    uint32_t frame_length;
    uint32_t frame_offset;

    //第二段用于跨FIFO边界，不在模块内复制整帧
    const uint8_t *frame2;
    uint32_t frame2_length;

    uint32_t token_counter;
    uint32_t pending_token;
    uint16_t max_packet_length;
    uint16_t pending_data_length;
    uint16_t pending_packet_length;

    uint8_t fid;
    bool frame_started;
    UsbUvcPayloadState_t state;

    uint8_t packet[USB_UVC_PAYLOAD_STORAGE_SIZE] __attribute__((aligned(4)));
} UsbUvcPayload_t;

//初始化零值对象，FID从0开始，重复初始化返回ALREADY_EXISTS
SysResult_t USB_UVC_PayloadInit(UsbUvcPayload_t *payload);

//仅空闲时复位新流的FID，保留Token计数以拒绝旧完成通知
SysResult_t USB_UVC_PayloadReset(UsbUvcPayload_t *payload);

//借用外部图像，长度必须非零，最大包长范围3到1023字节
SysResult_t USB_UVC_PayloadStartFrame(
    UsbUvcPayload_t *payload,
    const uint8_t *frame,
    uint32_t frame_length,
    uint16_t max_packet_length
);

//借用一段或两段外部图像，完成或取消确认前各段必须保持不变
SysResult_t USB_UVC_PayloadStartView(UsbUvcPayload_t *payload,
    const VideoFrameView_t *view, uint16_t max_packet_length);

//构造带UVC头的小包，真实完成确认前不推进偏移
SysResult_t USB_UVC_PayloadBuildPacket(
    UsbUvcPayload_t *payload,
    UsbUvcPayloadPacket_t *packet
);

//仅在底层真实完成后确认，匹配Token后才推进偏移
//若已经请求取消，则本次完成只结束取消，不提交整帧成功
SysResult_t USB_UVC_PayloadCompletePacket(UsbUvcPayload_t *payload, uint32_t token);

//请求取消，有待确认的包时返回BUSY并保留所有权
//没有待确认包则立即结束，已经构造过包的帧切换FID
SysResult_t USB_UVC_PayloadCancel(UsbUvcPayload_t *payload);

//底层确认Abort结束或明确从未接受发送后，匹配Token结束取消
//本函数不停止硬件；不得把普通错误或busy=false当成Abort确认
SysResult_t USB_UVC_PayloadAbortPacket(UsbUvcPayload_t *payload, uint32_t token);

#endif
