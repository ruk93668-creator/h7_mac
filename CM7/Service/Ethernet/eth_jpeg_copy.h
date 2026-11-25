//JPEG 私有副本只在源租约有效期间生成；不涉及 Camera/DMA 所有权。
#ifndef ETH_JPEG_COPY_H
#define ETH_JPEG_COPY_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    //环内第一段和可选的跨尾第二段，均由调用者保持只读租约。
    const uint8_t *data;
    const uint8_t *data2;
    //完整字节数及第二段字节数。
    uint32_t length;
    uint32_t tail;
    //允许读取的源环范围；第二段只能从环首开始。
    const uint8_t *fifo;
    uint32_t fifo_size;
} EthJpegView_t;

//成功表示完整副本、SOI/EOI 和 CRC 均已生成；失败不得公开 dst。
//dst 至少 capacity 字节，不能与源环重叠；crc 必须非空。
//barrier 非空时每复制最多八字节调用一次；生产 SDRAM 调用者须提供 DSB。
//仅由拥有源租约的任务串行调用，不获取或归还任何源资源。
bool eth_jpeg_copy(const EthJpegView_t *view, uint8_t *dst,
    uint32_t capacity, void (*barrier)(void), uint32_t *crc);

#endif
