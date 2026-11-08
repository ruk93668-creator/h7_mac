//单帧专项链接真实MJPEG FIFO，仅模拟未连接硬件的连续采集启动
#ifndef JPEG_TEST_FIFO_DEPENDENCY
#define JPEG_TEST_FIFO_DEPENDENCY

#ifndef __DMB
#define JPEG_TEST_LOCAL_DMB
#define __DMB() ((void)0)
#endif
#include "../../CM7/Media/Src/camera_mjpeg_fifo.c"
#ifdef JPEG_TEST_LOCAL_DMB
#undef __DMB
#undef JPEG_TEST_LOCAL_DMB
#endif

//记录独立测试是否进入连续采集，不伪造硬件帧或完成事件
volatile uint32_t g_jpeg_test_continuous_starts;

//连续启动仅供已有模式互斥用例，不模拟DMA实际搬运
SysResult_t BSP_DCMI_StartJpegContinuous(
    uint32_t *m0, uint32_t *m1, uint32_t words,
    const BspDcmiByteStreamOps_t *ops, void *context)
{
    (void)m0;
    (void)m1;
    (void)words;
    (void)ops;
    (void)context;
    g_jpeg_test_continuous_starts++;
    return SYS_OK;
}
#endif
