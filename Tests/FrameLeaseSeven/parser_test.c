#include <stdint.h>
#include <string.h>
static uint32_t first_failure;
#define assert(x) do {if(!(x) && !first_failure)first_failure=__LINE__;} while(0)
#include "../../CM7/Media/Src/camera_mjpeg_fifo.c"
#include "../../CM7/Service/usb_uvc_payload.c"
#define mjpeg_fixture original_fixture
#include "../CameraMjpegStage03B/jpeg_fixture.inc"
#undef mjpeg_fixture
//合法COM填充使每个16KiB块小于8帧，跨界帧不会先被READY丢弃策略淘汰
static uint8_t mjpeg_fixture[sizeof(original_fixture) + 2000U];

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
    memcpy(mjpeg_fixture, original_fixture, 2U);
    mjpeg_fixture[2] = 0xFFU; mjpeg_fixture[3] = 0xFEU;
    mjpeg_fixture[4] = 0x07U; mjpeg_fixture[5] = 0xCEU;
    memset(mjpeg_fixture + 6U, 0x55, 1996U);
    memcpy(mjpeg_fixture + 2002U, original_fixture + 2U, sizeof(original_fixture) - 2U);
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

//逐字节核对一到两段视图，不能把尾部填充算入JPEG
static void CheckFrame(const CameraFrame_t *frame, bool compare)
{
    uint32_t i, first = frame->size_bytes - frame->size_bytes2;
    assert(frame->width == 160U && frame->height == 120U);
    assert(frame->format == CAMERA_PIXEL_FORMAT_JPEG);
    assert(frame->size_bytes == sizeof(mjpeg_fixture));
    assert((uintptr_t)frame->data >= CAMERA_MJPEG_FIFO_BASE);
    assert((uintptr_t)frame->data + first <= CAMERA_MJPEG_DROP_BASE);
    if (frame->size_bytes2 != 0U)
        assert((uintptr_t)frame->data2 == CAMERA_MJPEG_FIFO_BASE);
    if (compare)
        for (i = 0U; i < frame->size_bytes; i++)
            assert((i < first ? frame->data[i] : frame->data2[i - first]) == mjpeg_fixture[i]);
}

//排空未借出的帧，每张都通过正式归还接口释放
static bool Drain(void)
{
    CameraFrame_t frame;
    bool wrapped = false;
    while (CameraMjpegFifo_GetFrame(&frame))
    {
        CheckFrame(&frame, true);
        wrapped |= frame.size_bytes2 != 0U;
        assert(CameraMjpegFifo_ReleaseFrame() == SYS_OK);
    }
    CheckOwnership();
    return wrapped;
}

//逐例执行，不通过更改正式逻辑来模拟通过
static void Run(unsigned id)
{
    CameraFrame_t frame;
    CameraConfig_t config = {160U,120U,CAMERA_PIXEL_FORMAT_JPEG};
    uint32_t i, previous, refs;
    bool wrapped = false;
    Reset();
    switch (id)
    {
        case 1:
            assert(CAMERA_MJPEG_FIFO_BASE >= 0xD02EE400UL);
            assert(CAMERA_MJPEG_REGION_END <= BSP_SDRAM_END);
            assert(dma_address[0] != dma_address[1]);
            break;
        case 2:
            previous = start_count;
            assert(CameraMjpegFifo_Start(NULL) == SYS_ERR_INVALID_ARG);
            config.width = 320U;
            assert(CameraMjpegFifo_Start(&config) == SYS_ERR_NOT_SUPPORTED);
            assert(start_count == previous);
            break;
        case 3:
            mock_ops->frame(mock_context);
            assert(CameraMjpegFifo_Process() == SYS_OK);
            assert(!CameraMjpegFifo_GetFrame(&frame));
            break;
        case 4:
            FeedBlock(true,true);
            assert(CameraMjpegFifo_GetFrame(&frame));
            CheckFrame(&frame,true);
            assert(CameraMjpegFifo_ReleaseFrame() == SYS_OK);
            break;
        case 5:
            for (i=0U;i<72U;i++) {
                uint32_t j; FeedBlock(true,true);
                for(j=0;j<s_fifo.ready_count;j++) {
                    MjpegFrame_t *r=&s_fifo.ready[(s_fifo.ready_head+j)%CAMERA_MJPEG_FRAME_QUEUE];
                    wrapped |= r->offset+r->length>CAMERA_MJPEG_FIFO_BYTES;
                }
                (void)Drain();
            }
            assert(wrapped);
            break;
        case 6:
            FeedBlock(true,true);
            assert(CameraMjpegFifo_GetFrame(&frame));
            for (i=0U;i<75U;i++) FeedBlock(true,true);
            CheckFrame(&frame,true);
            assert(s_fifo.dropped_blocks > 0U && s_fifo.epoch > 1U);
            assert(CameraMjpegFifo_ReleaseFrame() == SYS_OK);
            for (i=0U;i<8U;i++) FeedBlock(true,true);
            assert(CameraMjpegFifo_GetFrame(&frame));
            CheckFrame(&frame,true);
            break;
        case 7:
            for (i=0U;i<12U;i++) FeedBlock(true,true);
            assert(s_fifo.ready_count == CAMERA_MJPEG_MAX_LOANS-1U);
            assert(s_fifo.dropped_frames > 0U);
            break;
        case 8:
            FeedBlock(true,true);
            refs = s_fifo.blocks[0].references;
            assert(refs > 1U);
            assert(CameraMjpegFifo_GetFrame(&frame));
            assert(CameraMjpegFifo_ReleaseFrame() == SYS_OK);
            assert(s_fifo.blocks[0].references == ((s_fifo.partial.blocks & 1U) != 0U ? 1U : 0U));
            assert(s_fifo.blocks[0].state == (s_fifo.blocks[0].references == 0U ? MJPEG_BLOCK_FREE : MJPEG_BLOCK_SEALED));
            break;
        case 9:
            FeedBlock(false,true);
            assert(!CameraMjpegFifo_GetFrame(&frame));
            for (i=0U;i<8U;i++) mock_ops->frame(mock_context);
            assert(CameraMjpegFifo_Process() == SYS_OK);
            assert(CameraMjpegFifo_GetFrame(&frame));
            CheckFrame(&frame,true);
            break;
        case 10:
            fake_app_eoi = true;
            FeedBlock(true,true);
            assert(CameraMjpegFifo_GetFrame(&frame));
            CheckFrame(&frame,false);
            break;
        case 11:
            corrupt_sof = true;
            FeedBlock(true,true);
            assert(!CameraMjpegFifo_GetFrame(&frame));
            break;
        case 12:
            FeedBlock(true,true);
            assert(CameraMjpegFifo_GetFrame(&frame));
            CameraMjpegFifo_OnStopped();
            assert(CameraMjpegFifo_HasLoan());
            CheckFrame(&frame,true);
            assert(CameraMjpegFifo_Start(&config) == SYS_ERR_BUSY);
            assert(CameraMjpegFifo_ReleaseFrame() == SYS_OK);
            assert(CameraMjpegFifo_ReleaseFrame() == SYS_ERR_INVALID_STATE);
            assert(CameraMjpegFifo_Start(&config) == SYS_OK);
            break;
        case 13:
            CameraMjpegFifo_OnStopped();
            mock_start_result = SYS_ERR_IO;
            assert(CameraMjpegFifo_Start(&config) == SYS_ERR_IO);
            assert(CameraMjpegFifo_Start(&config) == SYS_ERR_BUSY);
            CameraMjpegFifo_OnStopped();
            mock_start_result = SYS_OK;
            assert(CameraMjpegFifo_Start(&config) == SYS_OK);
            break;
        case 14:
            CameraMjpegFifo_OnStopped();
            config.width = 320U;
            config.height = 240U;
            assert(CameraMjpegFifo_Start(&config) == SYS_OK);
            FeedBlock(true,true);

            //160×120样本不能冒充320×240，SOF尺寸必须匹配配置
            assert(!CameraMjpegFifo_GetFrame(&frame));
            break;
        case 15:
            CameraMjpegFifo_OnStopped();
            config.width = 640U;
            config.height = 480U;
            assert(CameraMjpegFifo_Start(&config) == SYS_OK);
            FeedBlock(true,true);

            //VGA同样必须核对真实SOF尺寸，不能交付原160×120样本
            assert(!CameraMjpegFifo_GetFrame(&frame));
            break;
        default: assert(0);
    }
    CheckOwnership();
}

//验证分段Payload不会提前推进，完成和取消仍遵循Token
static void PayloadCase(void)
{
    UsbUvcPayload_t payload = {0};
    UsbUvcPayloadPacket_t packet;
    VideoFrameView_t view = {0};
    uint32_t i, offset = 0U;
    uint32_t token;
    view.spans[0].data = mjpeg_fixture;
    view.spans[0].length = 5U;
    view.spans[1].data = mjpeg_fixture + 5U;
    view.spans[1].length = sizeof(mjpeg_fixture) - 5U;
    view.span_count = 2U;
    view.size_bytes = sizeof(mjpeg_fixture);
    assert(USB_UVC_PayloadInit(&payload) == SYS_OK);
    assert(USB_UVC_PayloadStartView(&payload,&view,1023U) == SYS_OK);
    while (payload.state != USB_UVC_PAYLOAD_IDLE)
    {
        assert(USB_UVC_PayloadBuildPacket(&payload,&packet) == SYS_OK);
        assert(payload.frame_offset == offset);
        for (i=2U;i<packet.length;i++) assert(packet.data[i] == mjpeg_fixture[offset+i-2U]);
        offset += packet.length - 2U;
        assert(((packet.data[1] & 2U) != 0U) == (offset == sizeof(mjpeg_fixture)));
        token = packet.token;
        assert(USB_UVC_PayloadCompletePacket(&payload,token) == SYS_OK);
        assert(USB_UVC_PayloadCompletePacket(&payload,token) != SYS_OK);
    }
    assert(offset == sizeof(mjpeg_fixture) && payload.fid == 1U);
    assert(USB_UVC_PayloadStartView(&payload,&view,1023U) == SYS_OK);
    assert(USB_UVC_PayloadBuildPacket(&payload,&packet) == SYS_OK);
    token = packet.token;
    assert(USB_UVC_PayloadCancel(&payload) == SYS_ERR_BUSY);
    assert(payload.frame != NULL && payload.frame2 != NULL);
    assert(USB_UVC_PayloadAbortPacket(&payload,token) == SYS_OK);
    assert(payload.frame == NULL && payload.frame2 == NULL);
    view.spans[1].length = 0U;
    assert(USB_UVC_PayloadStartView(&payload,&view,1023U) == SYS_ERR_INVALID_ARG);
}

uint32_t Stage02_TestRun(uint32_t id){if(id<15)Run(id+1);else PayloadCase();return first_failure;}
