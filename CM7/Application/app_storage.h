#ifndef APP_STORAGE_H
#define APP_STORAGE_H
#include "camera_config.h"
#include "storage_fs.h"

#define APP_STORAGE_SLOT_COUNT 4U
#define APP_STORAGE_FRAME_LIMIT 70000U
#define APP_STORAGE_SLOT_STRIDE 70016U
#define APP_STORAGE_POOL_BASE 0xD0384000UL
#define APP_STORAGE_POOL_END (APP_STORAGE_POOL_BASE + APP_STORAGE_SLOT_COUNT * APP_STORAGE_SLOT_STRIDE)

//SD用途与主输出独立，录像和抓拍互斥
typedef enum { APP_STORAGE_OFF = 0, APP_STORAGE_VIDEO, APP_STORAGE_SNAPSHOT } AppStorageMode_t;

//元数据随副本传递，时间是CameraTask取得帧的时刻，不是曝光时刻
typedef struct
{
    uint32_t frame_id;
    uint32_t timestamp_ms;
    uint32_t stream_epoch;
    uint16_t width;
    uint16_t height;
} AppFrameMeta_t;

//可修改的保存策略只在空闲时应用，不改变传感器时序
typedef struct
{
    uint32_t record_fps;
    uint32_t segment_bytes;
    uint32_t segment_ms;
    uint32_t total_bytes;
    uint32_t reserve_bytes;
    uint32_t max_segments;
} AppStorageConfig_t;

//诊断只用于观察，不作为其他任务释放资源的凭据
typedef struct
{
    uint32_t ready;
    uint32_t request;//1录像、2抓拍、3停止、4恢复；默认0，不自动写卡
    uint32_t state;//0空闲、1录像、2抓拍、3收尾、4故障
    uint32_t mode;
    uint32_t stop_reason;//1主动、2总量、3空间、4故障、5抓拍完成、6无帧超时
    uint32_t session;
    uint32_t requested;
    uint32_t saved_frames;
    uint32_t saved_files;
    uint32_t dropped_frames;
    uint32_t rejected_requests;
    uint32_t failures;
    uint32_t queue_depth;
    uint32_t queue_high_water;
    uint32_t max_copy_ms;
    uint32_t max_write_ms;
    uint32_t last_size;
    uint32_t last_crc;
    uint32_t read_crc;
    uint32_t file_number;
    uint32_t frame_id;
    uint32_t timestamp_ms;
    uint32_t stream_epoch;
    uint32_t measured_fps_milli;
    uint32_t stack_free_words;
    SysResult_t result;
    SysResult_t cleanup_result;
    StorageFsDiagnostics_t filesystem;
} AppStorageStatus_t;

//提供人工操作与诊断入口，不是历史SD验收任务
extern volatile AppStorageStatus_t g_storage_debug;

//Storage任务在BOOT后准备静态队列，不识卡或写入
SysResult_t App_StoragePrepare(const AppStorageConfig_t *config);

//请求录像，成功只表示命令进入有界队列
SysResult_t App_StorageStartRecording(void);

//请求下一张完整JPEG，不能与正在录像的会话混用
SysResult_t App_StorageRequestSnapshot(void);

//先禁止新副本，Storage任务随后排空队列并关闭文件
SysResult_t App_StorageStop(void);

//请求显式恢复，不把恢复成功当成原文件已保存
SysResult_t App_StorageRecover(void);

//取得诊断副本，不能用于决定Media或USB资源所有权
SysResult_t App_StorageGetStatus(AppStorageStatus_t *status);

//Camera任务查询当前是否需要存储帧，不取得Media所有权
bool App_StorageWantsFrame(uint32_t now);

//Camera任务在持帧期间复制两段JPEG，不能向Storage借出原指针
SysResult_t App_StorageOfferFrame(const CameraFrame_t *frame, const AppFrameMeta_t *meta);

//只有Storage任务调用，处理有界命令和副本
void App_StorageProcess(void);
#endif
