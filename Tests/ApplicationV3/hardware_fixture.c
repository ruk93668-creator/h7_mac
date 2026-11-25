#include "mjpeg_avi.h"
#include "hardware_fixture.h"
#include "ov5640_device.h"
#include "usb_mode.h"
#include "usb_uvc.h"
#include "sd_block.h"
#include "platform_time.h"
#include <string.h>
#include "app_messages.h"
#include "camera_mjpeg_fifo.h"
uint32_t g_network_frame_bytes = 20U;
uint32_t g_network_frame_ready, g_network_frame_releases, g_network_release_fail;
static CameraMjpegLease_t s_jpeg_network;

//仅宿主夹具注入被测Service可观察的底层事实
HardwareFixture_t g_hardware = {.card_present = true, .storage = {.state = 1U}};
static bool s_camera_initialized, s_camera_running, s_camera_held;
static CameraConfig_t s_camera_config;
static CameraCaptureStopFacts_t s_camera_stop_facts;
static CameraFrameLease_t s_frame_lease;
static uint32_t s_capture_generation;
static uint8_t s_sample;
static uint8_t s_jpeg_usb_data[16], s_jpeg_usb_tail[4];
static CameraMjpegLease_t s_jpeg_usb;
static bool s_uvc_descriptor_ready, s_uvc_registered;

uint32_t Platform_TimeGetMs(void)
{
    return g_hardware.now_ms;
}

static SysResult_t Fixture_Configure(Ov5640Device_t *camera, const CameraConfig_t *config)
{
    if (camera == NULL || config == NULL)
        return SYS_ERR_INVALID_ARG;
    ++g_hardware.sensor_configure_calls;
    return SYS_OK;
}

static const Ov5640Ops_t s_sensor_ops = {.configure = Fixture_Configure};
static Ov5640Device_t s_sensor = {.base = {.state = DEVICE_STATE_READY}, .ops = &s_sensor_ops};

Device_t *Device_Find(const char *name)
{
    return strcmp(name, "ov5640") == 0 ? &s_sensor.base : NULL;
}

SysResult_t Device_Start(Device_t *device)
{
    if (device->state != DEVICE_STATE_READY)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.sensor_start_calls;
    device->state = DEVICE_STATE_RUNNING;
    return SYS_OK;
}

SysResult_t Device_Stop(Device_t *device)
{
    if (device->state != DEVICE_STATE_RUNNING)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.sensor_stop_calls;
    device->state = g_hardware.sensor_stop_result == SYS_OK ? DEVICE_STATE_READY : DEVICE_STATE_ERROR;
    return g_hardware.sensor_stop_result;
}

SysResult_t CameraCapture_Init(const CameraConfig_t *config)
{
    if (s_camera_initialized)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.capture_init_calls;
    s_camera_initialized = true;
    s_camera_config = *config;
    return SYS_OK;
}

SysResult_t CameraCapture_StartStream(void)
{
    if (!s_camera_initialized || s_camera_running)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.capture_start_calls;
    ++s_capture_generation;
    s_camera_running = true;
    return g_hardware.camera_start_result;
}

void CameraCapture_Process(void)
{
}

SysResult_t CameraCapture_GetLastResult(void)
{
    return SYS_OK;
}

SysResult_t CameraCapture_GetStats(CameraCaptureStats_t *stats)
{
    *stats = (CameraCaptureStats_t){g_hardware.camera_completed, g_hardware.camera_dropped};
    return SYS_OK;
}

bool CameraCapture_StreamGetFrame(CameraFrame_t *frame)
{
    uint32_t bytes;
    if (!s_camera_running || !g_hardware.frame_ready || s_camera_held)
        return false;
    if (s_camera_config.format == CAMERA_PIXEL_FORMAT_JPEG)
        bytes = 12U;
    else if (CameraConfig_GetFrameSize(&s_camera_config, &bytes) != SYS_OK)
        return false;

    g_hardware.frame_ready = false;
    s_camera_held = true;
    *frame = (CameraFrame_t)
    {
        .width = s_camera_config.width,
        .height = s_camera_config.height,
        .format = s_camera_config.format,
        .data = &s_sample,
        .size_bytes = bytes
    };
    return true;
}

SysResult_t CameraCapture_StreamReleaseFrame(void)
{
    if (!s_camera_held)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.frame_release_calls;
    if (g_hardware.frame_release_result == SYS_OK)
        s_camera_held = false;
    return g_hardware.frame_release_result;
}

//A2的运输测试使用可控制的物理边界；A1真实物理账本另有联合专项。
SysResult_t CameraCapture_StreamAcquire(CameraFrameConsumer_t consumer,
    CameraFrameLease_t *lease, CameraFrame_t *frame)
{
    if (!CameraCapture_StreamGetFrame(frame))
        return SYS_ERR_BUSY;
    *lease = (CameraFrameLease_t){s_capture_generation, ++g_hardware.frame_acquire_calls, 0U, consumer};
    s_frame_lease = *lease;
    return SYS_OK;
}

SysResult_t CameraCapture_StreamReleaseLease(const CameraFrameLease_t *lease)
{
    if (!s_camera_held || lease->generation != s_frame_lease.generation ||
        lease->lease_id != s_frame_lease.lease_id || lease->buffer_id != s_frame_lease.buffer_id ||
        lease->consumer != s_frame_lease.consumer)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.frame_lease_releases;
    s_camera_held = false;
    return SYS_OK;
}

SysResult_t CameraCapture_Stop(void)
{
    ++g_hardware.capture_stop_calls;
    if (g_hardware.camera_stop_result == SYS_ERR_BUSY || s_camera_held || s_jpeg_usb.id != 0U)
        return SYS_ERR_BUSY;
    if (!g_hardware.stale_camera_stop)
        ++s_camera_stop_facts.attempt_sequence;
    s_camera_stop_facts.hardware_result = g_hardware.camera_stop_result;
    if (g_hardware.camera_stop_result == SYS_OK)
        s_camera_running = false;
    return g_hardware.camera_stop_result;
}

bool CameraCapture_IsStopped(void)
{
    return s_camera_initialized && !s_camera_running && !s_camera_held && s_jpeg_usb.id == 0U;
}

SysResult_t CameraCapture_GetStopFacts(CameraCaptureStopFacts_t *facts)
{
    *facts = s_camera_stop_facts;
    facts->flags = s_camera_initialized ? CAMERA_STOP_INITIALIZED : 0U;
    if (CameraCapture_IsStopped())
        facts->flags |= CAMERA_STOP_COMPLETE;
    return SYS_OK;
}

SysResult_t USB_Mode_SelectBootMode(UsbMode_t mode, SysResult_t (*register_class)(void))
{
    if (mode != USB_MODE_NONE || register_class != NULL || g_hardware.boot_mode_calls != 0U)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.boot_mode_calls;
    return SYS_OK;
}

SysResult_t USB_UVC_Configure(const VideoStreamConfig_t *config)
{
    (void)config;
    ++g_hardware.uvc_configure_calls;
    if (s_uvc_descriptor_ready)
        return SYS_ERR_INVALID_STATE;
    if (g_hardware.usb_configure_result == SYS_OK)
        s_uvc_descriptor_ready = true;
    return g_hardware.usb_configure_result;
}

SysResult_t USB_UVC_Register(void)
{
    ++g_hardware.uvc_register_calls;
    if (!s_uvc_descriptor_ready || s_uvc_registered || g_hardware.usb_core_stop_required)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.usb_plan_calls;
    //模拟Usb_PlanEndpoints在IO/TIMEOUT时留下的Core独立停止状态
    if (g_hardware.usb_plan_result == SYS_ERR_IO || g_hardware.usb_plan_result == SYS_ERR_TIMEOUT)
        g_hardware.usb_core_stop_required = true;
    if (g_hardware.usb_plan_result != SYS_OK)
        return g_hardware.usb_plan_result;
    if (g_hardware.usb_register_result != SYS_OK)
    {
        g_hardware.usb_core_stop_required = g_hardware.usb_register_partial;
        return g_hardware.usb_register_result;
    }
    s_uvc_registered = true;
    return SYS_OK;
}

SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *status)
{
    if (!g_hardware.uvc_present)
        return SYS_ERR_INVALID_STATE;
    *status = (UsbUvcStreamStatus_t){.state = USB_UVC_WAIT_FRAME,
        .result = g_hardware.usb_stream_result, .frame_owned = g_hardware.usb_frame_owned,
        .completed_frames = g_hardware.usb_completed_frames,
        .can_submit = g_hardware.raw_can_submit && !g_hardware.usb_frame_owned};
    return SYS_OK;
}

SysResult_t USB_UVC_ReadReaderProof(UsbUvcReaderProof_t *proof)
{
    *proof = g_hardware.raw_proof;
    return SYS_OK;
}

SysResult_t USB_UVC_SubmitFrame(const uint8_t *frame, uint32_t length)
{
    ++g_hardware.raw_submits;
    if (frame == NULL || length != 38400U)
        return SYS_ERR_INVALID_ARG;
    if (g_hardware.raw_submit_result == SYS_OK)
    {
        ++g_hardware.raw_proof.accepted_id;
        g_hardware.usb_frame_owned = true;
    }
    return g_hardware.raw_submit_result;
}

SysResult_t USB_UVC_CancelFrame(void)
{
    ++g_hardware.usb_cancel_calls;
    return g_hardware.usb_cancel_result;
}

void USB_UVC_Process(void)
{
    ++g_hardware.usb_process_calls;
}

SysResult_t USB_Device_Start(void)
{
    if (!s_uvc_registered || g_hardware.usb_core_stop_required)
        return SYS_ERR_INVALID_STATE;
    ++g_hardware.usb_start_calls;
    return SYS_OK;
}

SysResult_t USB_Device_Stop(void)
{
    ++g_hardware.usb_stop_calls;
    if (g_hardware.usb_stop_result == SYS_OK)
    {
        g_hardware.usb_core_stop_required = false;
        g_hardware.usb_configured = false;
        g_hardware.uvc_present = false;
    }
    return g_hardware.usb_stop_result;
}

bool USB_Device_IsConfigured(void)
{
    return g_hardware.usb_configured;
}

bool USB_Device_ReadLinkState(UsbDeviceLinkState_t *link)
{
    link->dev_state = g_hardware.usb_bus_state;
    if (link->dev_state == 0U)
        link->dev_state = g_hardware.usb_configured ? USBD_STATE_CONFIGURED : USBD_STATE_DEFAULT;
    link->dev_old_state = g_hardware.usb_previous_state != 0U ?
        g_hardware.usb_previous_state : USBD_STATE_CONFIGURED;
    link->started = g_hardware.usb_start_calls != 0U;
    link->stop_required = g_hardware.usb_core_stop_required;
    link->last_result = g_hardware.usb_result;
    return link->started && !link->stop_required && link->dev_state == USBD_STATE_CONFIGURED;
}

#ifdef APP_V3_USB_FAULT_TRACE_ENABLED
bool USB_Device_ReadR2Trace(UsbDeviceR2Trace_t *trace)
{
    UsbDeviceLinkState_t link;
    bool configured = USB_Device_ReadLinkState(&link);

    *trace = (UsbDeviceR2Trace_t){0};
    trace->dev_state = link.dev_state;
    trace->dev_old_state = link.dev_old_state;
    trace->started = link.started;
    trace->stop_required = link.stop_required;
    trace->last_result = link.last_result;
    trace->event_sequence = 4U;
    trace->event_count = 1U;
    trace->events[0] = (UsbDeviceTraceEvent_t)
    {
        4U, g_hardware.now_ms, USB_DEVICE_TRACE_DISCONNECT,
        trace->dev_state, trace->dev_old_state, 0U
    };
    return configured;
}
#endif

SysResult_t USB_Device_GetLastResult(void)
{
    return g_hardware.usb_result;
}

SysResult_t USB_Device_GetEpState(uint8_t endpoint, UsbDeviceEndpointState_t *state)
{
    if (endpoint != 0x81U)
        return SYS_ERR_INVALID_ARG;
    *state = (UsbDeviceEndpointState_t){.busy = g_hardware.endpoint_busy};
    return SYS_OK;
}

SysResult_t BSP_SDMMC_ReadDetect(uint32_t *raw, bool *present)
{
    *present = g_hardware.card_present;
    *raw = *present ? 0U : 1U;
    return SYS_OK;
}

bool SD_Block_IsOwner(SdBlockClient_t client)
{
    return client == SD_BLOCK_CLIENT_FATFS && g_hardware.storage_owner;
}

SysResult_t SD_Block_GetStatus(SdBlockStatus_t *status)
{
    *status = (SdBlockStatus_t){g_hardware.card_present, true, g_hardware.storage_busy, false};
    return g_hardware.storage_owner ? SYS_OK : SYS_ERR_INVALID_STATE;
}

SysResult_t StorageFS_Mount(bool writable, uint8_t partition)
{
    ++g_hardware.mount_calls;
    g_hardware.storage_writable = writable;
    g_hardware.partition = partition;
    if (g_hardware.mount_result != SYS_OK)
        return g_hardware.mount_result;
    g_hardware.storage.state = 2U;
    g_hardware.storage.writable = writable ? 1U : 0U;
    g_hardware.storage_owner = true;
    return SYS_OK;
}

SysResult_t StorageFS_Unmount(void)
{
    ++g_hardware.unmount_calls;
    if (g_hardware.unmount_result == SYS_ERR_BUSY)
        return SYS_ERR_BUSY;
    if (g_hardware.unmount_result != SYS_OK)
    {
        g_hardware.storage.state = 3U;
        g_hardware.storage.cleanup_result = g_hardware.unmount_result;
        return g_hardware.unmount_result;
    }
    g_hardware.storage.state = 1U;
    g_hardware.storage_owner = false;
    return SYS_OK;
}

SysResult_t StorageFS_Recover(void)
{
    ++g_hardware.recover_calls;
    if (g_hardware.recover_result != SYS_OK)
        return g_hardware.recover_result;
    g_hardware.storage.state = 1U;
    g_hardware.storage.cleanup_result = SYS_OK;
    g_hardware.storage_owner = false;
    return SYS_OK;
}

SysResult_t StorageFS_GetDiagnostics(StorageFsDiagnostics_t *diagnostics)
{
    *diagnostics = g_hardware.storage;
    return SYS_OK;
}

SysResult_t StorageFS_GetSpace(uint64_t *total_bytes, uint64_t *free_bytes)
{
    *total_bytes = 1024ULL * 512U;
    *free_bytes = 512ULL * 512U;
    return g_hardware.storage_owner ? SYS_OK : SYS_ERR_INVALID_STATE;
}


static SysResult_t Test_AcquireJpegStorage(CameraMjpegLease_t *lease, CameraFrame_t *frame)
{
    CameraFrameLease_t acquired;
    SysResult_t result = CameraCapture_StreamAcquire(CAMERA_FRAME_CONSUMER_STORAGE, &acquired, frame);
    if (result == SYS_OK)
    {
        *lease = (CameraMjpegLease_t){acquired.generation, acquired.lease_id, acquired.consumer};
        frame->size_bytes = 12U;
        frame->size_bytes2 = 4U;
        frame->data2 = frame->data;
    }
    return result;
}

static SysResult_t Test_ReleaseJpegStorage(const CameraMjpegLease_t *lease)
{
    const CameraFrameLease_t acquired = {lease->session, lease->id, 0U, lease->consumer};
    return CameraCapture_StreamReleaseLease(&acquired);
}

SysResult_t StorageFS_Open(const char *path, uint32_t mode, StorageFile_t *file)
{
    if (!g_hardware.storage_writable || mode != (STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW) ||
        path == NULL || path[11] != '.' || !g_hardware.storage_owner)
        return SYS_ERR_INVALID_STATE;
    *file = 1U;
    ++g_hardware.storage.open_files;
    return SYS_OK;
}

SysResult_t StorageFS_Close(StorageFile_t file)
{
    if (file != 1U || g_hardware.storage.open_files == 0U)
        return SYS_ERR_INVALID_STATE;
    --g_hardware.storage.open_files;
    return SYS_OK;
}

SysResult_t MjpegAvi_Begin(MjpegAvi_t *avi, StorageFile_t file, uint16_t width, uint16_t height, uint16_t fps)
{
    *avi = (MjpegAvi_t){.file = file, .width = width, .height = height, .rate = fps};
    return SYS_OK;
}

SysResult_t MjpegAvi_AppendFrame(MjpegAvi_t *avi, const CameraFrame_t *frame, uint32_t tick, uint32_t limit)
{
    (void)limit;
    ++g_hardware.record_writes;
    if (!s_camera_held || frame->size_bytes2 != 4U || frame->data2 == NULL)
        return SYS_ERR_PROTOCOL;
    if (g_hardware.record_write_result != SYS_OK)
    {
        avi->failed = true;
        return g_hardware.record_write_result;
    }
    if (avi->frames == 0U)
        avi->first_tick = tick;
    ++avi->frames;
    avi->last_tick = tick;
    return SYS_OK;
}

SysResult_t MjpegAvi_Finish(MjpegAvi_t *avi)
{
    ++g_hardware.record_finishes;
    if (s_camera_held || avi->frames == 0U || avi->failed)
        return SYS_ERR_INVALID_STATE;
    avi->finished = true;
    return SYS_OK;
}


SysResult_t CameraCapture_StreamAcquireJpeg(CameraFrameConsumer_t consumer,
    CameraMjpegLease_t *lease, CameraFrame_t *frame)
{
    if (consumer == CAMERA_FRAME_CONSUMER_ETHERNET)
    {
        uint8_t *data = (uint8_t *)(CAMERA_MJPEG_FIFO_BASE + CAMERA_MJPEG_FIFO_BYTES - (g_network_frame_bytes - 4U));
        uint8_t *tail = (uint8_t *)CAMERA_MJPEG_FIFO_BASE;
        if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA))
            return SYS_ERR_INVALID_STATE;
        if (!s_camera_running || !g_network_frame_ready || s_jpeg_network.id != 0U)
            return SYS_ERR_BUSY;
        memset(data, 0x55, g_network_frame_bytes - 4U);
        memset(tail, 0x55, 4U);
        data[0] = 0xffU;
        data[1] = 0xd8U;
        tail[2] = 0xffU;
        tail[3] = 0xd9U;
        g_network_frame_ready = 0U;
        s_jpeg_network = (CameraMjpegLease_t){s_capture_generation,
            ++g_hardware.frame_acquire_calls, consumer};
        *lease = s_jpeg_network;
        *frame = (CameraFrame_t){.width = s_camera_config.width, .height = s_camera_config.height,
            .format = CAMERA_PIXEL_FORMAT_JPEG, .data = data, .data2 = tail,
            .size_bytes = g_network_frame_bytes, .size_bytes2 = 4U};
        return SYS_OK;
    }
    if (consumer == CAMERA_FRAME_CONSUMER_STORAGE)
        return Test_AcquireJpegStorage(lease, frame);
    if (consumer != CAMERA_FRAME_CONSUMER_USB)
        return SYS_ERR_INVALID_ARG;

    if (!s_camera_running || !g_hardware.frame_ready || s_jpeg_usb.id != 0U)
        return SYS_ERR_BUSY;
    g_hardware.frame_ready = false;
    s_jpeg_usb = (CameraMjpegLease_t){s_capture_generation, ++g_hardware.frame_acquire_calls, consumer};
    *lease = s_jpeg_usb;
    *frame = (CameraFrame_t){.width = s_camera_config.width, .height = s_camera_config.height,
        .format = CAMERA_PIXEL_FORMAT_JPEG, .data = s_jpeg_usb_data, .data2 = s_jpeg_usb_tail,
        .size_bytes = g_hardware.jpeg_oversize ? 40001U : 20U, .size_bytes2 = 4U};
    return SYS_OK;
}

SysResult_t CameraCapture_StreamReleaseJpeg(const CameraMjpegLease_t *lease)
{
    if (lease == NULL)
        return SYS_ERR_INVALID_ARG;
    if (lease->consumer == CAMERA_FRAME_CONSUMER_ETHERNET)
    {
        if (!App_MessagesIsOwner(RUNTIME_TARGET_CAMERA) || lease->id == 0U ||
            lease->id != s_jpeg_network.id || lease->session != s_jpeg_network.session)
            return SYS_ERR_INVALID_STATE;
        if (g_network_release_fail)
        {
            --g_network_release_fail;
            return SYS_ERR_IO;
        }
        s_jpeg_network.id = 0U;
        ++g_network_frame_releases;
        return SYS_OK;
    }
    if (lease->consumer == CAMERA_FRAME_CONSUMER_STORAGE)
        return Test_ReleaseJpegStorage(lease);
    if (lease->consumer != s_jpeg_usb.consumer || lease->session != s_jpeg_usb.session ||
        lease->id == 0U || lease->id != s_jpeg_usb.id)
        return SYS_ERR_INVALID_STATE;
    s_jpeg_usb.id = 0U;
    ++g_hardware.jpeg_usb_releases;
    return SYS_OK;
}

SysResult_t USB_UVC_SubmitFrameView(const VideoFrameView_t *view)
{
    ++g_hardware.jpeg_submits;
    if (view->span_count != 2U || view->spans[0].length != 16U ||
        view->spans[1].length != 4U || !VideoFrameView_Valid(view))
        return SYS_ERR_PROTOCOL;
    if (g_hardware.raw_submit_result == SYS_OK)
    {
        ++g_hardware.raw_proof.accepted_id;
        g_hardware.usb_frame_owned = true;
    }
    return g_hardware.raw_submit_result;
}

#if APP_V3_JOINT_BOARD_TEST
void CameraMjpegFifo_ReadOccupancy(uint32_t *blocks, uint32_t *loans)
{
    *loans = (s_camera_held ? 1U : 0U) + (s_jpeg_usb.id != 0U ? 1U : 0U);
    *blocks = *loans + 2U;
}

void USB_UVC_ReadJointLink(UsbUvcJointLink_t *link)
{
    *link = (UsbUvcJointLink_t){.configured = g_hardware.usb_configured};
}

uint32_t USB_UVC_GetCompletedFrameCount(void)
{
    return g_hardware.usb_completed_frames;
}
#endif
