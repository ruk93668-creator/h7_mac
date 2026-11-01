#include "usb_uvc_payload.h"
#include <stddef.h>
#include <string.h>

//2字节Header仅使用FID、EOF和EOH
#define UVC_PAYLOAD_FID 0x01U
#define UVC_PAYLOAD_EOF 0x02U
#define UVC_PAYLOAD_EOH 0x80U

//检查对象已经初始化
static bool Payload_Initialized(const UsbUvcPayload_t *payload);

//检查图像边界和包长，避免减法下溢
static bool Payload_FrameValid(const UsbUvcPayload_t *payload);

//检查待完成包的长度和Token
static bool Payload_PendingValid(const UsbUvcPayload_t *payload);

//检查地址范围是否重叠，参数范围必须已经通过溢出检查
static bool Payload_Overlap(uintptr_t first, uintptr_t first_end,
    uintptr_t second, uintptr_t second_end);

//结束帧并释放引用，必要时为下一帧切换FID
static void Payload_FinishFrame(UsbUvcPayload_t *payload, bool toggle_fid);

SysResult_t USB_UVC_PayloadInit(UsbUvcPayload_t *payload)
{
    if (payload == NULL)
        return SYS_ERR_INVALID_ARG;
    if (Payload_Initialized(payload))
        return SYS_ERR_ALREADY_EXISTS;
    if (payload->state != USB_UVC_PAYLOAD_UNINITIALIZED)
        return SYS_ERR_INVALID_STATE;

    memset(payload, 0, sizeof(*payload));
    payload->state = USB_UVC_PAYLOAD_IDLE;
    return SYS_OK;
}

SysResult_t USB_UVC_PayloadReset(UsbUvcPayload_t *payload)
{
    if (payload == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Payload_Initialized(payload))
        return SYS_ERR_INVALID_STATE;
    if (payload->state != USB_UVC_PAYLOAD_IDLE)
        return SYS_ERR_BUSY;

    //仅空闲时允许开始新流，不能抹掉异步缓冲区所有权
    Payload_FinishFrame(payload, false);
    payload->fid = 0U;
    return SYS_OK;
}

SysResult_t USB_UVC_PayloadStartFrame(
    UsbUvcPayload_t *payload,
    const uint8_t *frame,
    uint32_t frame_length,
    uint16_t max_packet_length
)
{
    uintptr_t address;
    uintptr_t frame_end;
    uintptr_t object_address;

    if (payload == NULL || frame == NULL || frame_length == 0U ||
        max_packet_length <= USB_UVC_PAYLOAD_HEADER_SIZE ||
        max_packet_length > USB_UVC_PAYLOAD_MAX_PACKET_SIZE)
        return SYS_ERR_INVALID_ARG;
    if (!Payload_Initialized(payload))
        return SYS_ERR_INVALID_STATE;
    if (payload->state != USB_UVC_PAYLOAD_IDLE)
        return SYS_ERR_BUSY;
    if (payload->fid > 1U)
        return SYS_ERR_INVALID_STATE;

    //先检查整帧地址溢出，再判断是否覆盖模块自身
    address = (uintptr_t)frame;
    if (address > UINTPTR_MAX - (frame_length - 1U))
        return SYS_ERR_INVALID_ARG;
    frame_end = address + frame_length - 1U;
    object_address = (uintptr_t)payload;
    if (Payload_Overlap(address, frame_end, object_address,
        object_address + sizeof(*payload) - 1U))
        return SYS_ERR_INVALID_ARG;

    payload->frame = frame;
    payload->frame_length = frame_length;
    payload->frame_offset = 0U;
    payload->max_packet_length = max_packet_length;
    payload->frame_started = false;
    payload->state = USB_UVC_PAYLOAD_FRAME;
    return SYS_OK;
}

SysResult_t USB_UVC_PayloadBuildPacket(
    UsbUvcPayload_t *payload,
    UsbUvcPayloadPacket_t *packet
)
{
    uintptr_t output;
    uintptr_t object;
    uint32_t remaining;
    uint16_t data_length;
    uint16_t padded;
    uint16_t index;

    if (payload == NULL || packet == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Payload_Initialized(payload))
        return SYS_ERR_INVALID_STATE;

    //输出视图不能覆盖状态、内部包或只读图像
    output = (uintptr_t)packet;
    object = (uintptr_t)payload;
    if (output > UINTPTR_MAX - (sizeof(*packet) - 1U) ||
        Payload_Overlap(output, output + sizeof(*packet) - 1U,
            object, object + sizeof(*payload) - 1U))
        return SYS_ERR_INVALID_ARG;
    if (payload->frame != NULL &&
        Payload_Overlap(output, output + sizeof(*packet) - 1U,
            (uintptr_t)payload->frame, (uintptr_t)payload->frame + payload->frame_length - 1U))
        return SYS_ERR_INVALID_ARG;

    if (payload->state == USB_UVC_PAYLOAD_CANCELLING)
        return SYS_ERR_BUSY;
    if (payload->state == USB_UVC_PAYLOAD_PACKET)
    {
        //未确认时包保持不变，可在发送未被接受时重试
        if (!Payload_PendingValid(payload))
            return SYS_ERR_INVALID_STATE;
    }
    else
    {
        if (payload->state != USB_UVC_PAYLOAD_FRAME || !Payload_FrameValid(payload))
            return SYS_ERR_INVALID_STATE;

        //禁止Token回绕复用，Reset也不能重置此计数
        if (payload->token_counter == UINT32_MAX)
            return SYS_ERR_NO_RESOURCE;

        remaining = payload->frame_length - payload->frame_offset;
        data_length = (uint16_t)(payload->max_packet_length - USB_UVC_PAYLOAD_HEADER_SIZE);
        if (remaining < data_length)
            data_length = (uint16_t)remaining;

        payload->packet[0] = USB_UVC_PAYLOAD_HEADER_SIZE;
        payload->packet[1] = (uint8_t)(UVC_PAYLOAD_EOH | (payload->fid & UVC_PAYLOAD_FID));
        if (remaining == data_length)
            payload->packet[1] |= UVC_PAYLOAD_EOF;

        //只复制本包有效图像字节，不读取图像尾部以外的数据
        for (index = 0U; index < data_length; index++)
            payload->packet[index + USB_UVC_PAYLOAD_HEADER_SIZE] =
                payload->frame[payload->frame_offset + index];

        payload->pending_data_length = data_length;
        payload->pending_packet_length = (uint16_t)(data_length + USB_UVC_PAYLOAD_HEADER_SIZE);

        //为未来32位PIO读取清零最后不足一Word的尾部
        padded = (uint16_t)((payload->pending_packet_length + 3U) & ~3U);
        for (index = payload->pending_packet_length; index < padded; index++)
            payload->packet[index] = 0U;

        payload->token_counter++;
        payload->pending_token = payload->token_counter;
        payload->frame_started = true;
        payload->state = USB_UVC_PAYLOAD_PACKET;
    }

    packet->data = payload->packet;
    packet->length = payload->pending_packet_length;
    packet->token = payload->pending_token;
    return SYS_OK;
}

SysResult_t USB_UVC_PayloadCompletePacket(UsbUvcPayload_t *payload, uint32_t token)
{
    if (payload == NULL || token == 0U)
        return SYS_ERR_INVALID_ARG;
    if (!Payload_Initialized(payload) ||
        (payload->state != USB_UVC_PAYLOAD_PACKET && payload->state != USB_UVC_PAYLOAD_CANCELLING) ||
        !Payload_PendingValid(payload))
        return SYS_ERR_INVALID_STATE;
    if (token != payload->pending_token)
        return SYS_ERR_NOT_FOUND;

    if (payload->state == USB_UVC_PAYLOAD_CANCELLING)
    {
        //取消与完成交错时只结束取消，不把该帧交付为成功
        Payload_FinishFrame(payload, true);
        return SYS_OK;
    }

    //只有真实完成通知能够推进帧偏移
    payload->frame_offset += payload->pending_data_length;
    if (payload->frame_offset == payload->frame_length)
        Payload_FinishFrame(payload, true);
    else
    {
        payload->pending_token = 0U;
        payload->pending_data_length = 0U;
        payload->pending_packet_length = 0U;
        payload->state = USB_UVC_PAYLOAD_FRAME;
    }
    return SYS_OK;
}

SysResult_t USB_UVC_PayloadCancel(UsbUvcPayload_t *payload)
{
    if (payload == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!Payload_Initialized(payload))
        return SYS_ERR_INVALID_STATE;
    if (payload->state == USB_UVC_PAYLOAD_IDLE)
        return SYS_OK;
    if (payload->state == USB_UVC_PAYLOAD_PACKET || payload->state == USB_UVC_PAYLOAD_CANCELLING)
    {
        if (!Payload_PendingValid(payload))
            return SYS_ERR_INVALID_STATE;

        //只登记取消，等待底层明确归还包缓冲区
        payload->state = USB_UVC_PAYLOAD_CANCELLING;
        return SYS_ERR_BUSY;
    }
    if (!Payload_FrameValid(payload))
        return SYS_ERR_INVALID_STATE;

    //已构造过包的帧可能被主机看到，下一帧必须更换FID
    Payload_FinishFrame(payload, payload->frame_started);
    return SYS_OK;
}

SysResult_t USB_UVC_PayloadAbortPacket(UsbUvcPayload_t *payload, uint32_t token)
{
    if (payload == NULL || token == 0U)
        return SYS_ERR_INVALID_ARG;
    if (!Payload_Initialized(payload) || payload->state != USB_UVC_PAYLOAD_CANCELLING ||
        !Payload_PendingValid(payload))
        return SYS_ERR_INVALID_STATE;
    if (token != payload->pending_token)
        return SYS_ERR_NOT_FOUND;

    //调用方已确认不再被硬件访问，才释放图像和包引用
    Payload_FinishFrame(payload, true);
    return SYS_OK;
}

//内部功能函数实现

static bool Payload_Initialized(const UsbUvcPayload_t *payload)
{
    return payload->state >= USB_UVC_PAYLOAD_IDLE &&
        payload->state <= USB_UVC_PAYLOAD_CANCELLING;
}

static bool Payload_FrameValid(const UsbUvcPayload_t *payload)
{
    return payload->frame != NULL && payload->frame_length != 0U &&
        payload->frame_offset < payload->frame_length && payload->fid <= 1U &&
        payload->max_packet_length > USB_UVC_PAYLOAD_HEADER_SIZE &&
        payload->max_packet_length <= USB_UVC_PAYLOAD_MAX_PACKET_SIZE;
}

static bool Payload_PendingValid(const UsbUvcPayload_t *payload)
{
    return Payload_FrameValid(payload) && payload->frame_started &&
        payload->pending_token != 0U && payload->pending_token == payload->token_counter &&
        payload->pending_data_length != 0U &&
        payload->pending_data_length <= payload->max_packet_length - USB_UVC_PAYLOAD_HEADER_SIZE &&
        payload->pending_data_length <= payload->frame_length - payload->frame_offset &&
        payload->pending_packet_length == payload->pending_data_length + USB_UVC_PAYLOAD_HEADER_SIZE;
}

static bool Payload_Overlap(uintptr_t first, uintptr_t first_end,
    uintptr_t second, uintptr_t second_end)
{
    return first <= second_end && second <= first_end;
}

static void Payload_FinishFrame(UsbUvcPayload_t *payload, bool toggle_fid)
{
    if (toggle_fid)
        payload->fid ^= UVC_PAYLOAD_FID;
    payload->frame = NULL;
    payload->frame_length = 0U;
    payload->frame_offset = 0U;
    payload->max_packet_length = 0U;
    payload->pending_token = 0U;
    payload->pending_data_length = 0U;
    payload->pending_packet_length = 0U;
    payload->frame_started = false;
    payload->state = USB_UVC_PAYLOAD_IDLE;
}
