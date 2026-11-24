#include "../../../CM7/Media/Src/camera_capture.c"
#include "../../CameraMjpegStage03B/jpeg_fixture.inc"
#include <string.h>

volatile uint32_t g_failure, g_checks, g_stream_size, g_capture_size, g_lease_size;
uint32_t mock_primask;
static uint32_t s_rotations, s_stop_calls, s_words;
static bool s_write_then_fail;
static SysResult_t s_replace_result, s_stop_result;
static BspDcmiEvents_t s_events;
static BspDcmiFrameCallback_t s_raw_callback;
static const BspDcmiByteStreamOps_t *s_jpeg_ops;
static void *s_context;
static uint32_t *s_targets[2];
static const CameraConfig_t s_raw = {160U, 120U, CAMERA_PIXEL_FORMAT_YUV422};
static const CameraConfig_t s_jpeg = {160U, 120U, CAMERA_PIXEL_FORMAT_JPEG};

// 保留首个失败行；原始退出码由运行器记录
#define CHECK(condition) do { ++g_checks; if (!(condition)) { g_failure = __LINE__; return g_failure; } } while (0)

uint32_t Test_GetPrimask(void)
{
    return mock_primask;
}

void Test_DisableIrq(void)
{
    mock_primask = 1U;
}

void Test_SetPrimask(uint32_t value)
{
    mock_primask = value;
}

uint32_t Platform_TimeGetMs(void)
{
    return 0U;
}

bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *events)
{
    *events = s_events;
    s_events.flags = 0U;
    return events->flags != 0U;
}

SysResult_t BSP_DCMI_StartContinuous(uint32_t *m0, uint32_t *m1, uint32_t words,
    BspDcmiFrameCallback_t callback, void *context)
{
    s_targets[0] = m0;
    s_targets[1] = m1;
    s_words = words;
    s_raw_callback = callback;
    s_context = context;
    return SYS_OK;
}

SysResult_t BSP_DCMI_StartJpegContinuous(uint32_t *m0, uint32_t *m1, uint32_t words,
    const BspDcmiByteStreamOps_t *ops, void *context)
{
    s_targets[0] = m0;
    s_targets[1] = m1;
    s_words = words;
    s_jpeg_ops = ops;
    s_context = context;
    return SYS_OK;
}

SysResult_t BSP_DCMI_Stop(void)
{
    ++s_stop_calls;
    return s_stop_result;
}

SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t target, uint32_t *address)
{
    uint32_t index = (uint32_t)target;

    // 故障注入区分写前失败和硬件已改写但确认失败
    ++s_rotations;
    if (s_replace_result == SYS_OK || s_write_then_fail)
        s_targets[index] = address;
    return s_replace_result;
}

SysResult_t BSP_DCMI_SetEventCallback(BspDcmiEventCallback_t callback, void *context)
{
    (void)callback;
    (void)context;
    return SYS_OK;
}

SysResult_t BSP_DCMI_StartSnapshot(uint32_t *buffer, uint32_t words)
{
    (void)buffer;
    (void)words;
    return SYS_ERR_NOT_SUPPORTED;
}

SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *buffer, uint32_t words)
{
    return BSP_DCMI_StartSnapshot(buffer, words);
}

SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *received)
{
    (void)received;
    return SYS_ERR_NOT_SUPPORTED;
}

// 原 DMA 回调只接受完成目标；写入标记用于检查旧持有从未被覆盖
static SysResult_t Complete(uint32_t index, uint32_t marker)
{
    SysResult_t result;

    s_targets[index][0] = marker;
    result = s_raw_callback((BspDcmiTarget_t)index, s_context);
    if (result != SYS_OK)
    {
        s_events.flags = BSP_DCMI_EVENT_ERROR;
        s_events.result = result;
    }
    return result;
}

static uint32_t StartRaw(void)
{
    CHECK(CameraCapture_Init(&s_raw) == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_OK);
    CHECK(s_words == 9600U);
    CHECK(s_targets[0] != s_targets[1]);
    return 0U;
}

// 由实际物理槽逐一验证 DMA 地址与消费者的标记
static uint32_t VerifyHeld(const CameraFrameLease_t leases[], const CameraFrame_t frames[], uint32_t held)
{
    uint32_t consumer;

    CHECK(CameraStream_OwnershipValid(&s_capture.stream));
    CHECK(s_targets[0] == (uint32_t *)s_capture.stream.buffers[s_capture.stream.dma_m0_index].data);
    CHECK(s_targets[1] == (uint32_t *)s_capture.stream.buffers[s_capture.stream.dma_m1_index].data);
    for (consumer = 0U; consumer < CAMERA_FRAME_CONSUMER_COUNT; ++consumer)
    {
        if ((held & (1UL << consumer)) == 0U)
            continue;
        CHECK(frames[consumer].data != (const uint8_t *)s_targets[0]);
        CHECK(frames[consumer].data != (const uint8_t *)s_targets[1]);
        CHECK(frames[consumer].data[0] == consumer + 1U);
        CHECK(s_capture.stream.buffers[leases[consumer].buffer_id].state == CAMERA_BUFFER_IN_USE);
    }
    return 0U;
}

// 固定七槽达满载，再用两个真实轮换验证仅剩一块 FREE 与 READY
static uint32_t FullPool(CameraFrameLease_t leases[], CameraFrame_t frames[])
{
    uint32_t consumer, other;
    uint32_t free_count = 0U, ready_count = 0U;

    for (consumer = 0U; consumer < CAMERA_FRAME_CONSUMER_COUNT; ++consumer)
    {
        CHECK(Complete(consumer & 1U, consumer + 1U) == SYS_OK);
        CHECK(CameraCapture_StreamAcquire((CameraFrameConsumer_t)consumer,
            &leases[consumer], &frames[consumer]) == SYS_OK);
        for (other = 0U; other < consumer; ++other)
            CHECK(leases[other].buffer_id != leases[consumer].buffer_id);
    }
    for (consumer = 0U; consumer < CAMERA_STREAM_BUFFER_COUNT; ++consumer)
    {
        free_count += s_capture.stream.buffers[consumer].state == CAMERA_BUFFER_FREE;
        ready_count += s_capture.stream.buffers[consumer].state == CAMERA_BUFFER_READY;
    }
    CHECK(free_count == 1U && ready_count == 0U);
    CHECK(Complete(0U, 90U) == SYS_OK);
    CHECK(s_capture.stream.buffers[s_capture.stream.ready_index].state == CAMERA_BUFFER_READY);
    CHECK(s_capture.stream.dropped_frames == 0U);
    CHECK(Complete(1U, 91U) == SYS_OK);
    CHECK(s_capture.stream.dropped_frames == 1U);
    CHECK(VerifyHeld(leases, frames, 15U) == 0U);
    return 0U;
}

// 以阶乘进制遍历 24 个互异归还顺序，每次回收只改变对应的槽
static uint32_t ReleaseOrder(uint32_t rank)
{
    CameraFrameLease_t leases[CAMERA_FRAME_CONSUMER_COUNT];
    CameraFrame_t frames[CAMERA_FRAME_CONSUMER_COUNT];
    uint32_t order[] = {0U, 1U, 2U, 3U};
    uint32_t count, held = 15U;

    CHECK(FullPool(leases, frames) == 0U);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY && s_stop_calls == 0U);
    for (count = CAMERA_FRAME_CONSUMER_COUNT; count != 0U; --count)
    {
        uint32_t position = rank % count;
        uint32_t consumer = order[position];
        rank /= count;
        order[position] = order[count - 1U];

        CHECK(CameraCapture_StreamReleaseLease(&leases[consumer]) == SYS_OK);
        held &= ~(1UL << consumer);
        CHECK(CameraCapture_StreamReleaseLease(&leases[consumer]) == SYS_ERR_INVALID_STATE);
        CHECK(VerifyHeld(leases, frames, held) == 0U);
        CHECK(s_capture.stream.buffers[leases[consumer].buffer_id].state == CAMERA_BUFFER_FREE);
    }
    CHECK(CameraCapture_Stop() == SYS_OK && s_stop_calls == 1U && CameraCapture_IsStopped());
    return 0U;
}

static uint32_t InvalidIdentities(void)
{
    CameraFrameLease_t leases[CAMERA_FRAME_CONSUMER_COUNT], wrong, pending = {0};
    CameraFrame_t frames[CAMERA_FRAME_CONSUMER_COUNT], view = {0};

    CHECK(FullPool(leases, frames) == 0U);
    // 第五次申请同时覆盖重复消费者和未注册的第五种身份
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &pending, &view) == SYS_ERR_BUSY);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_COUNT, &pending, &view) == SYS_ERR_INVALID_ARG);
    CHECK(pending.lease_id == 0U && view.data == NULL);
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_ERR_INVALID_STATE);

    wrong = leases[0];
    wrong.consumer = CAMERA_FRAME_CONSUMER_STORAGE;
    CHECK(CameraCapture_StreamReleaseLease(&wrong) == SYS_ERR_INVALID_STATE);
    wrong = leases[0];
    wrong.buffer_id = leases[1].buffer_id;
    CHECK(CameraCapture_StreamReleaseLease(&wrong) == SYS_ERR_INVALID_STATE);
    wrong = leases[0];
    ++wrong.generation;
    CHECK(CameraCapture_StreamReleaseLease(&wrong) == SYS_ERR_INVALID_STATE);
    wrong = leases[0];
    ++wrong.lease_id;
    CHECK(CameraCapture_StreamReleaseLease(&wrong) == SYS_ERR_INVALID_STATE);
    CHECK(VerifyHeld(leases, frames, 15U) == 0U);
    return 0U;
}

static uint32_t RestartAndExhaustion(void)
{
    CameraFrameLease_t old_lease, new_lease;
    CameraFrame_t frame;

    CHECK(Complete(0U, 1U) == SYS_OK);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &old_lease, &frame) == SYS_OK);
    CHECK(CameraCapture_StreamReleaseLease(&old_lease) == SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_OK);
    CHECK(Complete(0U, 2U) == SYS_OK);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &new_lease, &frame) == SYS_OK);
    CHECK(old_lease.buffer_id == new_lease.buffer_id && old_lease.lease_id == new_lease.lease_id);
    CHECK(old_lease.generation != new_lease.generation);
    CHECK(CameraCapture_StreamReleaseLease(&old_lease) == SYS_ERR_INVALID_STATE);
    CHECK(CameraCapture_StreamReleaseLease(&new_lease) == SYS_OK);

    s_capture.stream.next_lease_id = UINT32_MAX - 1U;
    CHECK(Complete(1U, 3U) == SYS_OK);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &new_lease, &frame) == SYS_OK);
    CHECK(new_lease.lease_id == UINT32_MAX);
    CHECK(CameraCapture_StreamReleaseLease(&new_lease) == SYS_OK);
    CHECK(Complete(0U, 4U) == SYS_OK);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &new_lease, &frame) == SYS_ERR_NO_RESOURCE);
    CHECK(CameraCapture_Stop() == SYS_OK);
    s_capture.stream.generation = UINT32_MAX - 1U;
    CHECK(CameraCapture_StartStream() == SYS_OK);
    CHECK(s_capture.stream.generation == UINT32_MAX);
    CHECK(CameraCapture_Stop() == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_ERR_NO_RESOURCE && CameraCapture_IsStopped());
    return 0U;
}

static uint32_t FailedReplacement(bool write_occurred)
{
    CameraFrameLease_t leases[CAMERA_FRAME_CONSUMER_COUNT], pending = {0};
    CameraFrame_t frames[CAMERA_FRAME_CONSUMER_COUNT], view = {0};
    uint8_t m0, ready;
    uint32_t count, consumer;
    CameraCaptureStopFacts_t facts;

    CHECK(FullPool(leases, frames) == 0U);
    m0 = s_capture.stream.dma_m0_index;
    ready = s_capture.stream.ready_index;
    count = s_capture.stream.next_sequence;
    s_replace_result = SYS_ERR_IO;
    s_write_then_fail = write_occurred;
    CHECK(Complete(0U, 100U) == SYS_ERR_IO);
    CHECK(s_capture.stream.dma_m0_index == m0 && s_capture.stream.ready_index == ready);
    CHECK(s_capture.stream.next_sequence == count);
    CHECK((s_targets[0] != (uint32_t *)s_capture.stream.buffers[m0].data) == write_occurred);
    CHECK(leases[0].buffer_id != ready && frames[0].data[0] == 1U);

    // 即使任务尚未消费硬件错误，也不能借出可能已成为 DMA 目标的 READY
    CHECK(CameraCapture_StreamReleaseLease(&leases[0]) == SYS_OK);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &pending, &view) == SYS_ERR_INVALID_STATE);
    CHECK(pending.lease_id == 0U && view.data == NULL);
    s_stop_result = SYS_ERR_TIMEOUT;
    CameraCapture_Process();
    CHECK(CameraCapture_GetLastResult() == SYS_ERR_IO);
    CHECK(CameraCapture_GetStopFacts(&facts) == SYS_OK);
    CHECK((facts.flags & CAMERA_STOP_ERROR) != 0U && facts.hardware_result == SYS_ERR_TIMEOUT);
    CHECK(CameraCapture_StartStream() == SYS_ERR_INVALID_STATE);

    s_stop_result = SYS_OK;
    CHECK(CameraCapture_Stop() == SYS_OK);
    CHECK(CameraCapture_GetStopFacts(&facts) == SYS_OK);
    CHECK((facts.flags & CAMERA_STOP_DRAINING) != 0U);
    CHECK((facts.flags & CAMERA_STOP_PROTECTIVE_ATTEMPT) != 0U);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
    for (consumer = 1U; consumer < CAMERA_FRAME_CONSUMER_COUNT; ++consumer)
    {
        CHECK(!CameraCapture_IsStopped());
        CHECK(frames[consumer].data[0] == consumer + 1U);
        CHECK(CameraCapture_StreamReleaseLease(&leases[consumer]) == SYS_OK);
    }
    CHECK(CameraCapture_IsStopped());
    CHECK(CameraCapture_GetLastResult() == SYS_ERR_IO && s_stop_calls == 2U);
    return 0U;
}

static uint32_t LegacyAndLongHold(void)
{
    CameraFrame_t legacy, held, view;
    CameraFrameLease_t storage, usb;
    uint32_t cycle;

    CHECK(Complete(0U, 11U) == SYS_OK);
    CHECK(CameraCapture_StreamGetFrame(&legacy));
    CHECK(Complete(1U, 22U) == SYS_OK);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &usb, &view) == SYS_ERR_BUSY);
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_STORAGE, &storage, &held) == SYS_OK);
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_OK);
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_ERR_INVALID_STATE);
    for (cycle = 0U; cycle < 1000U; ++cycle)
    {
        CHECK(Complete(cycle & 1U, 99U) == SYS_OK);
        CHECK(held.data[0] == 22U);
    }
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &usb, &view) == SYS_OK);
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_ERR_INVALID_STATE);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY && s_stop_calls == 0U);
    CHECK(CameraCapture_StreamReleaseLease(&usb) == SYS_OK);
    CHECK(CameraCapture_StreamReleaseLease(&storage) == SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_OK);
    return 0U;
}

static uint32_t NoSafeTarget(void)
{
    uint32_t index;
    CameraFrameLease_t leases[CAMERA_FRAME_CONSUMER_COUNT];
    CameraFrame_t frames[CAMERA_FRAME_CONSUMER_COUNT];

    CHECK(FullPool(leases, frames) == 0U);
    index = s_capture.stream.ready_index;
    // 人为破坏最后一槽，验证不调用换址层，也不把隐藏持有写回 DMA
    s_capture.stream.buffers[index].state = CAMERA_BUFFER_IN_USE;
    s_capture.stream.ready_index = CAMERA_BUFFER_INDEX_INVALID;
    index = s_rotations;
    CHECK(Complete(0U, 88U) == SYS_ERR_INVALID_STATE);
    CHECK(s_rotations == index);
    return 0U;
}

// 使用原 FIFO 解析真实 JPEG 样本，并保留 SD 对同一 Loan 的既有契约
static uint32_t JpegRegression(void)
{
    CameraFrame_t frame;
    CameraFrameLease_t raw = {1U, 1U, 0U, CAMERA_FRAME_CONSUMER_USB};
    CameraMjpegLease_t sd;
    uint32_t index, next;
    uint8_t *data;

    CHECK(CameraCapture_Init(&s_jpeg) == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_OK);
    CHECK(s_words == CAMERA_MJPEG_BLOCK_BYTES / sizeof(uint32_t));
    CHECK(CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_USB, &raw, &frame) == SYS_ERR_NOT_SUPPORTED);
    CHECK(CameraCapture_StreamReleaseLease(&raw) == SYS_ERR_NOT_SUPPORTED);

    data = (uint8_t *)s_targets[0];
    for (index = 0U; index < CAMERA_MJPEG_BLOCK_BYTES; ++index)
        data[index] = mjpeg_fixture[index % sizeof(mjpeg_fixture)];
    for (index = 0U; index < CAMERA_MJPEG_BLOCK_BYTES / sizeof(mjpeg_fixture); ++index)
        s_jpeg_ops->frame(s_context);
    CHECK(s_jpeg_ops->block_complete(BSP_DCMI_TARGET_M0,
        (uint32_t)(uintptr_t)data, &next, s_context) == SYS_OK);
    CameraCapture_Process();
    CHECK(CameraCapture_StreamGetFrame(&frame));
    CHECK(frame.size_bytes == sizeof(mjpeg_fixture) && frame.size_bytes2 == 0U);
    CHECK(memcmp(frame.data, mjpeg_fixture, frame.size_bytes) == 0);
    CHECK(CameraMjpegFifo_RetainFrame(&frame, &sd) == SYS_OK);
    CHECK(CameraCapture_StreamReleaseJpeg(&sd) == SYS_ERR_INVALID_ARG);
    {
        CameraMjpegLease_t wrong = sd;
        wrong.consumer = CAMERA_FRAME_CONSUMER_USB;
        CHECK(CameraCapture_StreamReleaseJpeg(&wrong) == SYS_ERR_INVALID_STATE);
    }
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
    CHECK(CameraMjpegFifo_ReleaseLease(&sd) == SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_OK && CameraCapture_IsStopped());
    return 0U;
}

//直接取得SD位。旧USB归还不能清除SD位，旧会话不能归还新Loan。
static uint32_t JpegStorageOnly(void)
{
    CameraFrame_t frame;
    CameraMjpegLease_t lease, wrong;
    uint32_t index, next;
    uint8_t *data;

    CHECK(CameraCapture_Init(&s_jpeg) == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_OK);
    CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_STORAGE, &lease, &frame) == SYS_ERR_BUSY);
    data = (uint8_t *)s_targets[0];
    for (index = 0U; index < CAMERA_MJPEG_BLOCK_BYTES; ++index)
        data[index] = mjpeg_fixture[index % sizeof(mjpeg_fixture)];
    for (index = 0U; index < CAMERA_MJPEG_BLOCK_BYTES / sizeof(mjpeg_fixture); ++index)
        s_jpeg_ops->frame(s_context);
    CHECK(s_jpeg_ops->block_complete(BSP_DCMI_TARGET_M0,
        (uint32_t)(uintptr_t)data, &next, s_context) == SYS_OK);
    CameraCapture_Process();
    CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_STORAGE, &lease, &frame) == SYS_OK);
    CHECK(lease.session != 0U && lease.id != 0U && frame.size_bytes == sizeof(mjpeg_fixture));
    CHECK(memcmp(frame.data, mjpeg_fixture, frame.size_bytes) == 0);
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_ERR_INVALID_STATE);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
    wrong = lease;
    ++wrong.session;
    CHECK(CameraCapture_StreamReleaseJpeg(&wrong) != SYS_OK);
    CHECK(CameraCapture_StreamReleaseJpeg(&lease) == SYS_OK);
    CHECK(CameraCapture_StreamReleaseJpeg(&lease) != SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_OK && CameraCapture_IsStopped());
    return 0U;
}

//用真实FIFO在两个DMA块产生不同JPEG，分别持有及归还原Loan。
static uint32_t JpegIndependentConsumers(void)
{
    CameraFrame_t usb, sd;
    CameraMjpegLease_t u, s, wrong;
    uint32_t target, i, next;
    uint8_t *source;

    CHECK(CameraCapture_Init(&s_jpeg) == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_OK);
    for (target = 0U; target < 2U; ++target)
    {
        source = (uint8_t *)s_targets[target];
        for (i = 0U; i < CAMERA_MJPEG_BLOCK_BYTES; ++i)
            source[i] = mjpeg_fixture[i % sizeof(mjpeg_fixture)];
        for (i = 0U; i < CAMERA_MJPEG_BLOCK_BYTES / sizeof(mjpeg_fixture); ++i)
            s_jpeg_ops->frame(s_context);
        CHECK(s_jpeg_ops->block_complete((BspDcmiTarget_t)target,
            (uint32_t)(uintptr_t)source, &next, s_context) == SYS_OK);
        if (target != 0U)
            CHECK(next != (uint32_t)s_targets[0] && next != (uint32_t)s_targets[1]);
        CameraCapture_Process();
        if (target == 0U)
            CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_USB, &u, &usb) == SYS_OK);
        else
            CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_STORAGE, &s, &sd) == SYS_OK);
    }
    CHECK(u.session == s.session && u.id != s.id && usb.data != sd.data);
    CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_USB, &s, &sd) == SYS_ERR_BUSY);
    CHECK(!CameraCapture_StreamGetFrame(&sd));
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_ERR_INVALID_STATE);
    CHECK(u.consumer == CAMERA_FRAME_CONSUMER_USB && s.consumer == CAMERA_FRAME_CONSUMER_STORAGE);
    wrong = s;
    wrong.consumer = CAMERA_FRAME_CONSUMER_USB;
    CHECK(CameraCapture_StreamReleaseJpeg(&wrong) == SYS_ERR_INVALID_STATE);
    wrong = u;
    wrong.consumer = CAMERA_FRAME_CONSUMER_STORAGE;
    CHECK(CameraCapture_StreamReleaseJpeg(&wrong) == SYS_ERR_INVALID_STATE);
    wrong = u;
    wrong.id = s.id;
    CHECK(CameraCapture_StreamReleaseJpeg(&wrong) == SYS_ERR_INVALID_STATE);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
    CHECK(CameraCapture_StreamReleaseJpeg(&s) == SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
    CHECK(memcmp(usb.data, mjpeg_fixture, usb.size_bytes) == 0);
    CHECK(CameraCapture_StreamReleaseJpeg(&u) == SYS_OK);
    CHECK(CameraCapture_StreamReleaseJpeg(&u) != SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_OK && CameraCapture_IsStopped());
    return 0U;
}

//连续模拟JPEG块并借出USB显式租约，验证FIFO尾部两段字节逐一正确。
static uint32_t JpegUsbWrap(void)
{
    CameraFrame_t frame;
    CameraMjpegLease_t lease;
    uint32_t turn, next;
    const uint32_t split = 16U;
    bool begun = false;

    CHECK(CameraCapture_Init(&s_jpeg) == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_OK);
    for (turn = 0U; turn < 72U; ++turn)
    {
        uint32_t target = turn & 1U;
        uint8_t *source = (uint8_t *)s_targets[target];

        //末块注入JPEG前缀，下一块接续，避免测试帧被新帧淘汰。
        memset(source, 0, CAMERA_MJPEG_BLOCK_BYTES);
        if (begun)
        {
            CHECK((uintptr_t)source == CAMERA_MJPEG_FIFO_BASE);
            memcpy(source, mjpeg_fixture + split, sizeof(mjpeg_fixture) - split);
            s_jpeg_ops->frame(s_context);
        }
        else if ((uintptr_t)source == CAMERA_MJPEG_DROP_BASE - CAMERA_MJPEG_BLOCK_BYTES)
        {
            memcpy(source + CAMERA_MJPEG_BLOCK_BYTES - split, mjpeg_fixture, split);
            begun = true;
        }
        CHECK(s_jpeg_ops->block_complete((BspDcmiTarget_t)target,
            (uint32_t)(uintptr_t)source, &next, s_context) == SYS_OK);
        s_targets[target] = (uint32_t *)next;
        CameraCapture_Process();

        if (CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_USB, &lease, &frame) == SYS_OK)
        {
            bool wraps = frame.size_bytes2 != 0U;
            if (wraps)
            {
                uint32_t first = frame.size_bytes - frame.size_bytes2;
                CHECK(frame.size_bytes == sizeof(mjpeg_fixture) &&
                    (uintptr_t)frame.data2 == CAMERA_MJPEG_FIFO_BASE);
                CHECK(memcmp(frame.data, mjpeg_fixture, first) == 0);
                CHECK(memcmp(frame.data2, mjpeg_fixture + first, frame.size_bytes2) == 0);
            }
            CHECK(CameraCapture_StreamReleaseJpeg(&lease) == SYS_OK);
            if (wraps)
                return 0U;
        }
    }
    CHECK(false);
    return 0U;
}

static uint32_t JpegPublishForLease(uint32_t target)
{
    uint32_t i, next;
    uint8_t *source = (uint8_t *)s_targets[target];

    for (i = 0U; i < CAMERA_MJPEG_BLOCK_BYTES; ++i)
        source[i] = mjpeg_fixture[i % sizeof(mjpeg_fixture)];
    for (i = 0U; i < CAMERA_MJPEG_BLOCK_BYTES / sizeof(mjpeg_fixture); ++i)
        s_jpeg_ops->frame(s_context);
    CHECK(s_jpeg_ops->block_complete((BspDcmiTarget_t)target,
        (uint32_t)(uintptr_t)source, &next, s_context) == SYS_OK);
    s_targets[target] = (uint32_t *)(uintptr_t)next;
    CameraCapture_Process();
    return 0U;
}

static uint32_t JpegFourConsumers(void)
{
    CameraMjpegLease_t leases[CAMERA_FRAME_CONSUMER_COUNT], scratch;
    CameraFrame_t frames[CAMERA_FRAME_CONSUMER_COUNT], view;
    const uint32_t order[] = {2U, 0U, 3U, 1U};
    uint32_t i, j;

    CHECK(CameraCapture_Init(&s_jpeg) == SYS_OK);
    CHECK(CameraCapture_StartStream() == SYS_OK);
    CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_COUNT, &scratch,
        &view) == SYS_ERR_INVALID_ARG);
    for (i = 0U; i < CAMERA_FRAME_CONSUMER_COUNT; ++i)
    {
        CHECK(JpegPublishForLease(i & 1U) == 0U);
        CHECK(CameraCapture_StreamAcquireJpeg((CameraFrameConsumer_t)i,
            &leases[i], &frames[i]) == SYS_OK);
        CHECK(leases[i].consumer == (CameraFrameConsumer_t)i);
        for (j = 0U; j < i; ++j)
            CHECK(frames[j].data != frames[i].data && leases[j].id != leases[i].id);
    }

    CHECK(JpegPublishForLease(0U) == 0U);
    CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_STORAGE,
        &scratch, &view) == SYS_ERR_BUSY);
    CHECK(CameraCapture_StreamGetFrame(&view));
    CHECK(CameraCapture_StreamReleaseFrame() == SYS_OK);
    CHECK(CameraMjpegFifo_ReleaseLease(&leases[0]) == SYS_ERR_INVALID_ARG);

    for (i = 0U; i < CAMERA_FRAME_CONSUMER_COUNT; ++i)
    {
        j = order[i];
        CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
        CHECK(memcmp(frames[j].data, mjpeg_fixture, frames[j].size_bytes) == 0);
        CHECK(CameraCapture_StreamReleaseJpeg(&leases[j]) == SYS_OK);
    }
    CHECK(CameraCapture_Stop() == SYS_OK && CameraCapture_IsStopped());
    CHECK(CameraCapture_StartStream() == SYS_OK);
    CHECK(JpegPublishForLease(0U) == 0U);
    CHECK(CameraCapture_StreamAcquireJpeg(CAMERA_FRAME_CONSUMER_USB, &scratch, &view) == SYS_OK);
    CHECK(scratch.id == leases[0].id && scratch.session != leases[0].session);
    CHECK(CameraCapture_StreamReleaseJpeg(&leases[0]) == SYS_ERR_INVALID_ARG);
    CHECK(CameraCapture_Stop() == SYS_ERR_BUSY);
    CHECK(CameraCapture_StreamReleaseJpeg(&scratch) == SYS_OK);
    CHECK(CameraCapture_Stop() == SYS_OK && CameraCapture_IsStopped());
    return 0U;
}

uint32_t A1_TestRun(uint32_t scenario)
{
    g_stream_size = sizeof(CameraStream_t);
    g_capture_size = sizeof(CameraCapture_t);
    g_lease_size = sizeof(CameraFrameLease_t);
    if (scenario == 30U)
        return JpegRegression();
    if (scenario == 31U)
        return JpegStorageOnly();
    if (scenario == 32U)
        return JpegIndependentConsumers();
    if (scenario == 33U)
        return JpegUsbWrap();
    if (scenario == 34U)
        return JpegFourConsumers();
    CHECK(StartRaw() == 0U);
    if (scenario < 24U)
        return ReleaseOrder(scenario);
    switch (scenario)
    {
        case 24U: return InvalidIdentities();
        case 25U: return RestartAndExhaustion();
        case 26U: return FailedReplacement(false);
        case 27U: return FailedReplacement(true);
        case 28U: return LegacyAndLongHold();
        case 29U: return NoSafeTarget();
        default: return 1U;
    }
}
