#ifndef TEST_V3_HARDWARE_FIXTURE_H
#define TEST_V3_HARDWARE_FIXTURE_H

#include "camera_capture.h"
#include "storage_fs.h"
#include "usb_device.h"
#include "usb_uvc.h"

typedef struct
{
    uint32_t now_ms;
    uint32_t boot_mode_calls;
    uint32_t sensor_configure_calls, sensor_start_calls, sensor_stop_calls;
    uint32_t capture_init_calls, capture_start_calls, capture_stop_calls, frame_release_calls;
    SysResult_t sensor_stop_result, camera_start_result, camera_stop_result, frame_release_result;
    bool frame_ready, stale_camera_stop;
    uint32_t frame_acquire_calls, frame_lease_releases;
    uint32_t camera_completed, camera_dropped;
    uint32_t uvc_configure_calls, uvc_register_calls, usb_start_calls, usb_stop_calls, usb_cancel_calls;
    SysResult_t usb_stop_result, usb_result, usb_cancel_result, usb_stream_result;
    uint32_t usb_plan_calls, usb_process_calls;
    SysResult_t usb_configure_result, usb_register_result, usb_plan_result;
    bool usb_register_partial, usb_core_stop_required;
    bool usb_configured, uvc_present, usb_frame_owned, endpoint_busy;
    bool raw_can_submit;
    uint32_t raw_submits;
    SysResult_t raw_submit_result;
    UsbUvcReaderProof_t raw_proof;
    uint8_t usb_bus_state, usb_previous_state;
    uint32_t record_writes, record_finishes;
    SysResult_t record_write_result;
    uint32_t mount_calls, unmount_calls, recover_calls;
    SysResult_t mount_result, unmount_result, recover_result;
    bool card_present, storage_owner, storage_writable, storage_busy;
    uint8_t partition;
    StorageFsDiagnostics_t storage;
} HardwareFixture_t;

extern HardwareFixture_t g_hardware;

#endif
