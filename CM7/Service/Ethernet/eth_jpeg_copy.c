#include "eth_jpeg_copy.h"
#include <stddef.h>
#include <string.h>

static void EthJpeg_CopySpan(uint8_t *dst, const uint8_t *src,
    uint32_t length, void (*barrier)(void))
{
    if (barrier == NULL)
    {
        memcpy(dst, src, length);
        return;
    }
    while (length != 0U)
    {
        uint32_t amount = length > 8U ? 8U : length;
        memcpy(dst, src, amount);
        barrier();
        dst += amount;
        src += amount;
        length -= amount;
    }
}

bool eth_jpeg_copy(const EthJpegView_t *view, uint8_t *dst,
    uint32_t capacity, void (*barrier)(void), uint32_t *crc)
{
    static const uint32_t table[16] =
    {
        0x00000000U, 0x1db71064U, 0x3b6e20c8U, 0x26d930acU,
        0x76dc4190U, 0x6b6b51f4U, 0x4db26158U, 0x5005713cU,
        0xedb88320U, 0xf00f9344U, 0xd6d6a3e8U, 0xcb61b38cU,
        0x9b64c2b0U, 0x86d3d2d4U, 0xa00ae278U, 0xbdbdf21cU
    };
    uintptr_t base, start, target;
    uint32_t first, value = UINT32_MAX, index;
    if (view == NULL || dst == NULL || crc == NULL || view->fifo == NULL ||
        view->data == NULL || view->length < 4U || view->length > capacity ||
        view->length > UINT16_MAX || view->tail >= view->length)
        return false;
    base = (uintptr_t)view->fifo;
    start = (uintptr_t)view->data;
    target = (uintptr_t)dst;
    if (view->fifo_size > UINTPTR_MAX - base || capacity > UINTPTR_MAX - target ||
        start < base || start - base >= view->fifo_size ||
        (target < base + view->fifo_size && target + capacity > base))
        return false;
    first = view->length - view->tail;
    if (first > view->fifo_size - (start - base) ||
        (view->tail && (first != view->fifo_size - (start - base) ||
            view->data2 != view->fifo || view->tail > start - base)) ||
        (!view->tail && view->data2 != NULL))
        return false;
    EthJpeg_CopySpan(dst, view->data, first, barrier);
    if (view->tail)
        EthJpeg_CopySpan(dst + first, view->data2, view->tail, barrier);
    if (dst[0] != 0xffU || dst[1] != 0xd8U ||
        dst[view->length - 2U] != 0xffU || dst[view->length - 1U] != 0xd9U)
        return false;
    for (index = 0U; index < view->length; ++index)
    {
        value ^= dst[index];
        value = table[value & 15U] ^ (value >> 4U);
        value = table[value & 15U] ^ (value >> 4U);
    }
    *crc = ~value;
    return true;
}
