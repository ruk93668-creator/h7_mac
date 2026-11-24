#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "sys_result.h"

//图像格式，开机时选择
typedef enum
{
	CAMERA_PIXEL_FORMAT_Y8 = 0U, //每像素1字节灰度
	CAMERA_PIXEL_FORMAT_RGB565, //每像素2字节，低字节在前
	CAMERA_PIXEL_FORMAT_YUV422, //Y0 U0 Y1 V0，两个像素4字节
    CAMERA_PIXEL_FORMAT_JPEG   //可变长度压缩数据，不按像素数计算帧长
} CameraPixelFormat_t;

//摄像头输出配置，由Application统一选择
typedef struct
{
	//该图像或配置的水平像素数
	uint16_t width;
	//该图像或配置的垂直像素数
	uint16_t height;
	//确定像素解释方式和可用的图像传输路径
	CameraPixelFormat_t format;
} CameraConfig_t;

//描述一张完整图像
typedef struct
{
    //该图像或配置的水平像素数
    uint16_t width;
    //该图像或配置的垂直像素数
    uint16_t height;

    //确定像素解释方式和可用的图像传输路径
    CameraPixelFormat_t format;

    //调用者可读取的图像地址，受当前租约或帧引用保护
    const uint8_t *data;
    //完整图像的有效字节总数
    uint32_t size_bytes;

    //跨FIFO末尾时使用第二段，第一段长度为size_bytes减size_bytes2
    const uint8_t *data2;
    //跨环尾第二段的有效字节数，零表示连续视图
    uint32_t size_bytes2;

} CameraFrame_t;

// Raw 和 JPEG 共享消费者枚举，身份与具体输出通道无关。
typedef enum
{
    CAMERA_FRAME_CONSUMER_USB = 0,
    CAMERA_FRAME_CONSUMER_ETHERNET,
    CAMERA_FRAME_CONSUMER_WIFI,
    CAMERA_FRAME_CONSUMER_STORAGE,
    CAMERA_FRAME_CONSUMER_COUNT
} CameraFrameConsumer_t;

//判断是否为可变长度格式，不表示当前采集模式已支持
static inline bool CameraConfig_IsVariableLength(CameraPixelFormat_t format)
{
    return format == CAMERA_PIXEL_FORMAT_JPEG;
}

//判断当前固件接入的JPEG尺寸，不表示已经完成对应实板验收
static inline bool CameraConfig_IsJpegSizeSupported(const CameraConfig_t *config)
{
    return config != NULL &&
        ((config->width == 160U && config->height == 120U) ||
         (config->width == 320U && config->height == 240U) ||
         (config->width == 640U && config->height == 480U));
}

//仅计算固定长度图像内存，JPEG返回不支持且不修改输出长度
static inline SysResult_t CameraConfig_GetFrameSize(
    const CameraConfig_t *config,
    uint32_t *bytes)
{
    uint32_t bpp;
    uint32_t pixels;

    if (config == NULL || bytes == NULL || config->width == 0U || config->height == 0U)
        return SYS_ERR_INVALID_ARG;

    switch (config->format)
    {
        case CAMERA_PIXEL_FORMAT_Y8: bpp = 1U; break;
        case CAMERA_PIXEL_FORMAT_RGB565: bpp = 2U; break;
        case CAMERA_PIXEL_FORMAT_YUV422:
            if ((config->width & 1U) != 0U)
                return SYS_ERR_INVALID_ARG;
            bpp = 2U;
            break;
        case CAMERA_PIXEL_FORMAT_JPEG: return SYS_ERR_NOT_SUPPORTED;
        default: return SYS_ERR_INVALID_ARG;
    }

    pixels = (uint32_t)config->width * config->height;
    if (pixels > UINT32_MAX / bpp)
        return SYS_ERR_INVALID_ARG;

    *bytes = pixels * bpp;
    return SYS_OK;
}

#endif
