#ifndef CM7_V3_RECORD_BOARD_H
#define CM7_V3_RECORD_BOARD_H

#ifndef APP_V3_RECORD_BOARD_TEST
#define APP_V3_RECORD_BOARD_TEST 0
#endif

#if APP_V3_RECORD_BOARD_TEST != 0 && APP_V3_RECORD_BOARD_TEST != 1
#error Invalid APP_V3_RECORD_BOARD_TEST selection
#endif

#if APP_V3_RECORD_BOARD_TEST
#include "board_smoke.h"
#include "supervisor.h"
#include "storage_fs.h"
#include "bsp_sdmmc.h"

#if APP_V3_BOARD_SMOKE != APP_V3_BOARD_SMOKE_OFF || APP_V3_BOARD_RAW_UVC || APP_V3_RAW_UVC_TEST || APP_VIDEO_TEST_MODE
#error Recording board test cannot run with another board or video test
#endif
#if APP_VIDEO_MJPEG_MODE != 1 || !defined(APP_STORAGE_PARTITION) || APP_STORAGE_PARTITION < 1 || APP_STORAGE_PARTITION > 4
#error Recording board test requires JPEG and an explicit FAT partition 1..4
#endif

typedef struct
{
    uint32_t command;
    uint32_t approved_partition;
    uint32_t file_number;
} AppV3RecordBoardCommand_t;

typedef struct
{
    AppV3SupervisorStatus_t supervisor;
    uint32_t configured_partition;
    uint32_t issued;
    uint32_t last_command;
    SysResult_t dispatch_result;
    uint32_t elapsed_ms;
    bool command_pending;
    bool stack_valid;
    uint32_t stack_remaining_words[RUNTIME_TARGET_COUNT];
} AppV3RecordBoardStatus_t;

typedef struct
{
    AppV3HardwareReport_t hardware;
    StorageFsDiagnostics_t filesystem;
    BspSdmmcIoDiagnostics_t io;
    SysResult_t io_snapshot_result;
    uint32_t file_number;
    uint32_t file;
    uint32_t frames;
    uint32_t movi_end;
    uint32_t first_tick;
    uint32_t last_tick;
    uint32_t offer;
    uint32_t returned;
    uint32_t ack;
    uint32_t confirmed;
    uint32_t request_id;
    uint32_t session;
    uint32_t lease_id;
    uint32_t finish_ms;
    uint32_t filesystem_valid;
    SysResult_t finish_result;
    SysResult_t close_result;
    SysResult_t unmount_result;
    bool finish_attempted;
    bool close_attempted;
    bool unmount_attempted;
    bool fatfs_owner;
    bool admission;
    uint32_t frame_state;
} AppV3RecordBoardStorage_t;

typedef struct
{
    uint32_t confirmed;
    uint32_t request_id;
    uint32_t session;
    uint32_t lease_id;
} AppV3RecordBoardCamera_t;

//Watch先写参数，再写命令；Control只在自身轮次调用Supervisor。
extern volatile AppV3RecordBoardCommand_t g_app_v3_record_command;
extern volatile AppV3RecordBoardStatus_t g_app_v3_record_board;
extern volatile AppV3RecordBoardStorage_t g_app_v3_record_storage;
extern volatile AppV3RecordBoardCamera_t g_app_v3_record_camera;
void AppV3_RecordBoardPoll(void);
#endif

#endif
