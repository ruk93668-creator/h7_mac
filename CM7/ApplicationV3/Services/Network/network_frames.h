//NetworkTask 的 Camera 消息适配；不调用 CameraCapture 或 BSP。
#ifndef APP_V3_NETWORK_FRAMES_H
#define APP_V3_NETWORK_FRAMES_H

#include "eth_mjpeg.h"
#include <stdbool.h>
#include "sys_result.h"

//只允许 NetworkTask 调用；返回常驻动态 JPEG Source。
const EthMjpegSource_t *NetworkFrames_Source(void);
//每轮限量处理帧消息；accepting 为 false 时只取消申请和归还租约。
void NetworkFrames_Poll(bool accepting);
//关闭新帧准入，不撤销已经公开的 Camera 租约。
void NetworkFrames_BeginStop(void);
//只有归还确认已消费、原帧事务退休且私有副本已释放时才返回 true。
bool NetworkFrames_Retired(void);

#endif
