#include "jpeg_frame.h"

//按逻辑偏移读取双段帧，边界由入口统一检查
static uint8_t Jpeg_Byte(const CameraFrame_t *frame, uint32_t offset);

//检查JPEG结构，不把首尾标记当作解码成功证明
SysResult_t JpegFrame_Validate(const CameraFrame_t *f)
{
    uint32_t pos = 2U, length, width = 0U, height = 0U;
    uint8_t marker;
    bool sof = false, scan = false;

    //先确认真实长度和两段指针，拒绝空帧和非法拼接
    if (f == NULL || f->format != CAMERA_PIXEL_FORMAT_JPEG || f->data == NULL ||
        f->size_bytes < 4U || f->size_bytes2 >= f->size_bytes ||
        (f->size_bytes2 != 0U && f->data2 == NULL))
        return SYS_ERR_INVALID_ARG;
    if (Jpeg_Byte(f, 0U) != 0xFFU || Jpeg_Byte(f, 1U) != 0xD8U)
        return SYS_ERR_PROTOCOL;

    while (pos < f->size_bytes)
    {
        if (Jpeg_Byte(f, pos++) != 0xFFU)
            return SYS_ERR_PROTOCOL;
        while (pos < f->size_bytes && Jpeg_Byte(f, pos) == 0xFFU)
            pos++;
        if (pos >= f->size_bytes)
            return SYS_ERR_PROTOCOL;
        marker = Jpeg_Byte(f, pos++);

        //EOI必须结束真实数据，不把尾部填充当JPEG的一部分
        if (marker == 0xD9U)
            return sof && scan && pos == f->size_bytes ? SYS_OK : SYS_ERR_PROTOCOL;
        if (marker == 0U || marker == 0xD8U || (marker >= 0xD0U && marker <= 0xD7U))
            return SYS_ERR_PROTOCOL;
        if (marker == 1U)
            continue;
        if (pos + 2U > f->size_bytes)
            return SYS_ERR_PROTOCOL;
        length = ((uint32_t)Jpeg_Byte(f, pos) << 8) | Jpeg_Byte(f, pos + 1U);
        if (length < 2U || length > f->size_bytes - pos)
            return SYS_ERR_PROTOCOL;
        if (marker == 0xC0U || marker == 0xC2U)
        {
            if (length < 8U)
                return SYS_ERR_PROTOCOL;
            height = ((uint32_t)Jpeg_Byte(f, pos + 3U) << 8) | Jpeg_Byte(f, pos + 4U);
            width = ((uint32_t)Jpeg_Byte(f, pos + 5U) << 8) | Jpeg_Byte(f, pos + 6U);
            if (width != f->width || height != f->height || width == 0U || height == 0U)
                return SYS_ERR_PROTOCOL;
            sof = true;
        }
        pos += length;
        if (marker == 0xDAU)
        {
            if (!sof)
                return SYS_ERR_PROTOCOL;
            scan = true;

            //熵编码只跳过填充字节和Restart，其他标记回到结构解析
            while (pos < f->size_bytes)
            {
                if (Jpeg_Byte(f, pos) != 0xFFU)
                {
                    pos++;
                    continue;
                }
                if (pos + 1U >= f->size_bytes)
                    return SYS_ERR_PROTOCOL;
                marker = Jpeg_Byte(f, pos + 1U);
                if (marker == 0U || (marker >= 0xD0U && marker <= 0xD7U))
                    pos += 2U;
                else
                    break;
            }
        }
    }
    return SYS_ERR_PROTOCOL;
}

//CRC可按块继续更新，调用方以UINT32_MAX开始、最后取反
uint32_t JpegFrame_Crc32(const uint8_t *data, uint32_t size, uint32_t crc)
{
    uint32_t i, bit;
    for (i = 0U; i < size; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
    }
    return crc;
}

//第一段长度由总长减去第二段得到
static uint8_t Jpeg_Byte(const CameraFrame_t *f, uint32_t offset)
{
    uint32_t first = f->size_bytes - f->size_bytes2;
    return offset < first ? f->data[offset] : f->data2[offset - first];
}
