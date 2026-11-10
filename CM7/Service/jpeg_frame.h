#ifndef JPEG_FRAME_H
#define JPEG_FRAME_H
#include "camera_config.h"

//在原帧有效期间校验单段或双段JPEG结构与尺寸，不执行解码
SysResult_t JpegFrame_Validate(const CameraFrame_t *frame);

//按分段顺序计算CRC32，调用者保证字节仍归自己持有
uint32_t JpegFrame_Crc32(const uint8_t *data, uint32_t size, uint32_t crc);
#endif
