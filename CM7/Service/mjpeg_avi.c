#include "mjpeg_avi.h"
#include <string.h>

//索引放入独立SDRAM区，不占用任务栈
static uint32_t (*const s_index)[4] = (uint32_t (*)[4])MJPEG_AVI_INDEX_BASE;

//显式编码32位小端字段，不能依赖编译器结构填充
static void Avi_U32(uint8_t *p, uint32_t value);

//构造固定224字节AVI 1.0头，所有长度在结束时回填
static void Avi_Header(const MjpegAvi_t *avi, uint8_t *header, bool complete, uint32_t end);

//严格检查部分写入，不能把FR_OK短写当成功
static SysResult_t Avi_Write(StorageFile_t file, const void *data, uint32_t size);

//在空文件写入未完成头，不提前声明索引存在
SysResult_t MjpegAvi_Begin(MjpegAvi_t *a, StorageFile_t file, uint16_t width, uint16_t height, uint16_t fps)
{
    uint8_t header[224];
    SysResult_t result;
    uint32_t size;
    if (a == NULL || width == 0U || height == 0U || fps == 0U || fps > 60U)
        return SYS_ERR_INVALID_ARG;
    result = StorageFS_GetSize(file, &size);
    if (result != SYS_OK || size != 0U)
        return result != SYS_OK ? result : SYS_ERR_INVALID_STATE;
    memset(a, 0, sizeof(*a));
    a->file = file;
    a->width = width;
    a->height = height;
    a->rate = fps;
    a->scale = 1U;
    a->position = 224U;
    Avi_Header(a, header, false, 224U);
    result = Avi_Write(file, header, sizeof(header));
    a->failed = result != SYS_OK;
    return result;
}

//写入00dc和偶数字节填充，idx1大小包含在预算里
SysResult_t MjpegAvi_Append(MjpegAvi_t *a, const uint8_t *data, uint32_t size, uint32_t tick, uint32_t limit)
{
    uint8_t chunk[8], zero = 0U;
    uint32_t needed;
    SysResult_t result;
    if (a == NULL || data == NULL || size < 4U || size > 70000U)
        return SYS_ERR_INVALID_ARG;
    if (a->failed || a->finished || a->file == 0U)
        return SYS_ERR_INVALID_STATE;
    if (a->frames >= MJPEG_AVI_MAX_FRAMES)
        return SYS_ERR_NO_RESOURCE;
    needed = 8U + size + (size & 1U) + 8U + (a->frames + 1U) * 16U;
    if (a->position > limit || needed > limit - a->position)
        return SYS_ERR_NO_RESOURCE;
    memcpy(chunk, "00dc", 4U);
    Avi_U32(chunk + 4U, size);
    result = Avi_Write(a->file, chunk, sizeof(chunk));
    if (result == SYS_OK)
        result = Avi_Write(a->file, data, size);
    if (result == SYS_OK && (size & 1U) != 0U)
        result = Avi_Write(a->file, &zero, 1U);
    if (result != SYS_OK)
    {
        a->failed = true;
        return result;
    }

    //相对movi FOURCC的偏移，第一帧为4；JPEG均是关键帧
    s_index[a->frames][0] = 0x63643030UL;
    s_index[a->frames][1] = 0x10U;
    s_index[a->frames][2] = a->position - 220U;
    s_index[a->frames][3] = size;
    if (a->frames == 0U)
        a->first_tick = tick;
    a->last_tick = tick;
    a->frames++;
    a->position += 8U + size + (size & 1U);
    if (size > a->max_frame)
        a->max_frame = size;
    return SYS_OK;
}

//正常停止才建立完整索引和平均帧率，不承诺突然断电可播放
SysResult_t MjpegAvi_Finish(MjpegAvi_t *a)
{
    uint8_t header[224], chunk[8], batch[256];
    uint32_t end, i, j, n, duration;
    SysResult_t result;
    if (a == NULL || a->failed || a->finished || a->file == 0U || a->frames == 0U ||
        a->frames > MJPEG_AVI_MAX_FRAMES || a->rate == 0U || a->scale == 0U ||
        a->position < 224U || a->position > 0x10000000UL)
        return SYS_ERR_INVALID_STATE;

    //固定帧率AVI使用本段实际平均时间，不把目标FPS冒充实测FPS
    duration = a->last_tick - a->first_tick;
    if (a->frames > 1U && duration != 0U && duration < 0x7FFFFFFFUL)
    {
        duration += 1000U * a->scale / a->rate;
        a->scale = duration;
        a->rate = a->frames * 1000U;
    }
    memcpy(chunk, "idx1", 4U);
    Avi_U32(chunk + 4U, a->frames * 16U);
    result = Avi_Write(a->file, chunk, sizeof(chunk));
    for (i = 0U; i < a->frames && result == SYS_OK; i += n)
    {
        n = a->frames - i;
        if (n > 16U)
            n = 16U;
        for (j = 0U; j < n * 4U; j++)
            Avi_U32(batch + j * 4U, s_index[i + j / 4U][j % 4U]);
        result = Avi_Write(a->file, batch, n * 16U);
    }
    end = a->position + 8U + a->frames * 16U;
    if (result == SYS_OK)
        result = StorageFS_Seek(a->file, 0U);
    if (result == SYS_OK)
    {
        Avi_Header(a, header, true, end);
        result = Avi_Write(a->file, header, sizeof(header));
    }
    if (result == SYS_OK)
        result = StorageFS_Sync(a->file);
    a->failed = result != SYS_OK;
    a->finished = result == SYS_OK;
    return result;
}

//构造RIFF、hdrl、strl和movi头
static void Avi_Header(const MjpegAvi_t *a, uint8_t *h, bool complete, uint32_t end)
{
    memset(h, 0, 224U);
    memcpy(h, "RIFF", 4U);
    Avi_U32(h + 4U, end - 8U);
    memcpy(h + 8U, "AVI ", 4U);
    memcpy(h + 12U, "LIST", 4U);
    Avi_U32(h + 16U, 192U);
    memcpy(h + 20U, "hdrlavih", 8U);
    Avi_U32(h + 28U, 56U);
    Avi_U32(h + 32U, (uint32_t)((uint64_t)1000000U * a->scale / a->rate));
    Avi_U32(h + 44U, complete ? 0x10U : 0U);
    Avi_U32(h + 48U, a->frames);
    Avi_U32(h + 56U, 1U);
    Avi_U32(h + 60U, a->max_frame);
    Avi_U32(h + 64U, a->width);
    Avi_U32(h + 68U, a->height);
    memcpy(h + 88U, "LIST", 4U);
    Avi_U32(h + 92U, 116U);
    memcpy(h + 96U, "strlstrh", 8U);
    Avi_U32(h + 104U, 56U);
    memcpy(h + 108U, "vidsMJPG", 8U);
    Avi_U32(h + 128U, a->scale);
    Avi_U32(h + 132U, a->rate);
    Avi_U32(h + 140U, a->frames);
    Avi_U32(h + 144U, a->max_frame);
    Avi_U32(h + 148U, UINT32_MAX);
    h[160] = (uint8_t)a->width;
    h[161] = (uint8_t)(a->width >> 8);
    h[162] = (uint8_t)a->height;
    h[163] = (uint8_t)(a->height >> 8);
    memcpy(h + 164U, "strf", 4U);
    Avi_U32(h + 168U, 40U);
    Avi_U32(h + 172U, 40U);
    Avi_U32(h + 176U, a->width);
    Avi_U32(h + 180U, a->height);
    h[184] = 1U;
    h[186] = 24U;
    memcpy(h + 188U, "MJPG", 4U);
    Avi_U32(h + 192U, a->max_frame);
    memcpy(h + 212U, "LIST", 4U);
    Avi_U32(h + 216U, a->position - 220U);
    memcpy(h + 220U, "movi", 4U);
}

//写入长度不完整时立刻保留失败
static SysResult_t Avi_Write(StorageFile_t file, const void *data, uint32_t size)
{
    uint32_t done = 0U, n, amount;
    SysResult_t result;
    while (done < size)
    {
        amount = size - done;
        if (amount > 32768U)
            amount = 32768U;
        result = StorageFS_Write(file, (const uint8_t *)data + done, amount, &n);
        if (result != SYS_OK || n != amount)
            return result != SYS_OK ? result : SYS_ERR_NO_RESOURCE;
        done += n;
    }
    return SYS_OK;
}

//所有协议字段显式编码，ARMCC5与主机测试使用同一字节序
static void Avi_U32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
