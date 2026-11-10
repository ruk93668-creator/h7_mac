#include <stdint.h>
#include <string.h>
static uint32_t first_failure;
#define assert(x) do {if(!(x) && !first_failure)first_failure=__LINE__;} while(0)
#include "camera_mjpeg_fifo.h"
#ifdef CADENCE_SMALL_BLOCK
#undef CAMERA_MJPEG_BLOCK_BYTES
#undef CAMERA_MJPEG_FIFO_BYTES
#define CAMERA_MJPEG_BLOCK_BYTES (8UL * 1024UL)
#define CAMERA_MJPEG_FIFO_BYTES (512UL * 1024UL)
#else
#undef CAMERA_MJPEG_BLOCK_BYTES
#undef CAMERA_MJPEG_FIFO_BYTES
#define CAMERA_MJPEG_BLOCK_BYTES (16UL * 1024UL)
#define CAMERA_MJPEG_FIFO_BYTES (1024UL * 1024UL)
#endif
#include "../../CM7/Media/Src/camera_mjpeg_fifo.c"
#include "../../CM7/Service/usb_uvc_payload.c"
#define mjpeg_fixture original_fixture
#include "../CameraMjpegStage03B/jpeg_fixture.inc"
#undef mjpeg_fixture
//合法COM填充使每个16KiB块小于8帧，跨界帧不会先被READY丢弃策略淘汰
static uint8_t mjpeg_fixture[8888U];

uint32_t test_ipsr,test_primask,test_basepri;
static uint32_t dma_address[2];
static const BspDcmiByteStreamOps_t *mock_ops;
static void *mock_context;
static SysResult_t mock_start_result;
static uint32_t start_count;
static uint32_t source_offset;
static uint8_t target;
static bool corrupt_sof;
static bool fake_app_eoi;

//只模拟持续DMA目标，不伪造FRAME或TC的相互替代
SysResult_t BSP_DCMI_StartJpegContinuous(uint32_t *m0, uint32_t *m1,
    uint32_t words, const BspDcmiByteStreamOps_t *ops, void *context)
{
    assert(words == CAMERA_MJPEG_BLOCK_BYTES / 4U);
    dma_address[0] = (uint32_t)(uintptr_t)m0;
    dma_address[1] = (uint32_t)(uintptr_t)m1;
    mock_ops = ops;
    mock_context = context;
    start_count++;
    return mock_start_result;
}

//核对实际引用总数，包括READY、借出帧和正在解析的图像
static void CheckOwnership(void)
{
    uint32_t i, j, refs;
    for (i = 0U; i < CAMERA_MJPEG_BLOCK_COUNT; i++)
    {
        uint64_t bit = UINT64_C(1) << i;
        refs = (s_fifo.partial.blocks & bit) != 0U;
        for (j = 0U; j < CAMERA_MJPEG_MAX_LOANS; j++)
            if ((s_fifo.loans[j].usb_owned || s_fifo.loans[j].sd_owned) &&
                (s_fifo.loans[j].frame.blocks & bit) != 0U)
                refs++;
        for (j = 0U; j < s_fifo.ready_count; j++)
            refs += (s_fifo.ready[(s_fifo.ready_head + j) % CAMERA_MJPEG_FRAME_QUEUE].blocks & bit) != 0U;
        assert(s_fifo.blocks[i].references == refs);
        if (s_fifo.blocks[i].state == MJPEG_BLOCK_FREE)
            assert(refs == 0U);
        if (s_fifo.blocks[i].state == MJPEG_BLOCK_DMA)
            assert(refs == 0U && !s_fifo.blocks[i].parsed);
    }
    assert(s_fifo.ready_count <= CAMERA_MJPEG_FRAME_QUEUE);
    assert(s_fifo.completed_count <= CAMERA_MJPEG_BLOCK_COUNT);
    assert(test_primask == 0U);
}

//模拟调用者已经确认Stop成功后的新一轮启动
static void Reset(void)
{
    CameraConfig_t config = {160U, 120U, CAMERA_PIXEL_FORMAT_JPEG};
    uint32_t padding = sizeof(mjpeg_fixture) - sizeof(original_fixture);
    memcpy(mjpeg_fixture, original_fixture, 2U);
    mjpeg_fixture[2] = 0xFFU; mjpeg_fixture[3] = 0xFEU;
    mjpeg_fixture[4] = (uint8_t)((padding - 2U) >> 8);
    mjpeg_fixture[5] = (uint8_t)(padding - 2U);
    memset(mjpeg_fixture + 6U, 0x55, padding - 4U);
    memcpy(mjpeg_fixture + padding + 2U, original_fixture + 2U, sizeof(original_fixture) - 2U);
    if (s_fifo.loaned)
        assert(CameraMjpegFifo_ReleaseFrame() == SYS_OK);
    CameraMjpegFifo_OnStopped();
    mock_start_result = SYS_OK;
    corrupt_sof = false;
    fake_app_eoi = false;
    source_offset = 0U;
    target = 0U;
    assert(CameraMjpegFifo_Start(&config) == SYS_OK);
    CheckOwnership();
}

//模拟传感器连续输出，图像复制仅存在于测试夹具
static void FeedBlock(bool frame_irq, bool process)
{
    uint32_t i, next, frames = 0U;
    uint8_t *data = (uint8_t *)(uintptr_t)dma_address[target];
    for (i = 0U; i < CAMERA_MJPEG_BLOCK_BYTES; i++)
    {
        uint32_t p = source_offset % sizeof(mjpeg_fixture);
        uint8_t value = mjpeg_fixture[p];
        if (fake_app_eoi && (p == 6U || p == 7U))
            value = p == 6U ? 0xFFU : 0xD9U;
        if (corrupt_sof && p >= 4U && mjpeg_fixture[p - 4U] == 0xFFU &&
            mjpeg_fixture[p - 3U] == 0xC0U)
            value = 0U;
        data[i] = value;
        source_offset++;
        if (source_offset % sizeof(mjpeg_fixture) == 0U)
            frames++;
    }
    if (frame_irq)
        while (frames-- != 0U)
            mock_ops->frame(mock_context);
    assert(mock_ops->block_complete((BspDcmiTarget_t)target,
        dma_address[target], &next, mock_context) == SYS_OK);
    dma_address[target] = next;
    target ^= 1U;
    if (process)
    {
        //处理预算有限，多次调用模拟Camera任务再次获得运行机会
        for (i = 0U; i < 3U; i++)
            assert(CameraMjpegFifo_Process() == SYS_OK);
    }
    CheckOwnership();
}


//只验证数据交付节奏，不模拟CPU运行耗时，不代表实板FPS
volatile uint32_t g_cadence[6];
uint32_t Stage02_TestRun(uint32_t id)
{
 uint32_t ms,bytes_to,offset=0,filled=0,next,remaining=0,next_frame=0,usb=0,tc=0;
 bool started=false;CameraFrame_t frame;(void)id;
 Reset();
 for(ms=1;ms<=11000;ms++) {
  bytes_to=ms*(30U*sizeof(mjpeg_fixture))/1000U;
  while(offset<bytes_to) {
   ((uint8_t *)(uintptr_t)dma_address[target])[filled++]=mjpeg_fixture[offset%sizeof(mjpeg_fixture)];offset++;
   if(offset%sizeof(mjpeg_fixture)==0)mock_ops->frame(mock_context);
   if(filled==CAMERA_MJPEG_BLOCK_BYTES) {
    assert(mock_ops->block_complete((BspDcmiTarget_t)target,dma_address[target],&next,mock_context)==SYS_OK);
    dma_address[target]=next;target^=1;filled=0;tc++;
   }
  }
  assert(CameraMjpegFifo_Process()==SYS_OK);
  if(remaining) {
   if(!started&&ms>=next_frame){started=true;next_frame=ms+40;}
   if(started&&--remaining==0){assert(CameraMjpegFifo_ReleaseFrame()==SYS_OK);if(ms>1000)usb++;}
  }
  if(!remaining&&CameraMjpegFifo_GetFrame(&frame)) {
   assert(frame.size_bytes==sizeof(mjpeg_fixture));remaining=(frame.size_bytes+1020)/1021;started=false;
  }
 }
 g_cadence[0]=usb;g_cadence[1]=s_fifo.completed_frames;g_cadence[2]=s_fifo.dropped_frames;
 g_cadence[3]=tc;g_cadence[4]=CAMERA_MJPEG_BLOCK_BYTES;g_cadence[5]=sizeof(mjpeg_fixture);
 return first_failure;
}
