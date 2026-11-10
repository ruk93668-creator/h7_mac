#ifndef APP_STORAGE_H
#define APP_STORAGE_H
#include "camera_config.h"
#include "storage_fs.h"
#include "camera_mjpeg_fifo.h"

//SD最多持有一张原帧，归还确认前不接收下一张
#define APP_STORAGE_SLOT_COUNT 1U
#define APP_STORAGE_FRAME_LIMIT 70000U

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
    uint32_t max_offer_ms;
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

    //等待Camera确认的SD归还数及最近归还结果
    uint32_t pending_returns;
    uint32_t returned_frames;
    SysResult_t return_result;
    StorageFsDiagnostics_t filesystem;
} AppStorageStatus_t;

//提供人工操作与诊断入口，不是历史SD验收任务
extern volatile AppStorageStatus_t g_storage_debug;

//保存单个阶段的墙钟耗时，包含抢占和阻塞时间
typedef struct
{
    uint32_t samples;//已完成采样次数
    uint32_t last_ms;//最近耗时
    uint32_t max_ms;//历史峰值
    uint32_t average_ms;//累计平均，整数毫秒
    uint64_t total_ms;//累计耗时，避免32位回绕
} AppStorageTimingSample_t;

//只供诊断观察，不参与任务或帧所有权决策
typedef struct
{
    AppStorageTimingSample_t validate;//JPEG结构校验
    AppStorageTimingSample_t crc;//整帧CRC计算
    AppStorageTimingSample_t file_write;//文件准备、写入及本轮同步
    AppStorageTimingSample_t frame_process;//整帧处理，范围与max_write_ms一致
    AppStorageTimingSample_t queue_wait;//入队到Storage开始处理
    AppStorageTimingSample_t return_wait;//归还请求到Camera确认
    AppStorageTimingSample_t next_frame_wait;//确认归还到下一帧入队，含FPS限制
    AppStorageTimingSample_t free_to_check;//归还确认到Camera首次检查SD需求
    AppStorageTimingSample_t check_to_eligible;//首次检查到首次满足录像间隔
    AppStorageTimingSample_t eligible_to_offer;//满足条件到成功入队，包含取帧与分发
} AppStorageTimingDebug_t;

//从复位开始累计，不随会话清零，避免跨任务清零竞争
extern volatile AppStorageTimingDebug_t g_storage_timing_debug;


//Storage任务在BOOT后准备静态队列，不识卡或写入
SysResult_t App_StoragePrepare(const AppStorageConfig_t *config);

//请求录像，成功只表示命令进入有界队列
SysResult_t App_StorageStartRecording(void);

//请求下一张完整JPEG，不能与正在录像的会话混用
SysResult_t App_StorageRequestSnapshot(void);

//先禁止新租借，Storage任务随后排空队列并关闭文件
SysResult_t App_StorageStop(void);

//请求显式恢复，不把恢复成功当成原文件已保存
SysResult_t App_StorageRecover(void);

//取得诊断副本，不能用于决定Media或USB资源所有权
SysResult_t App_StorageGetStatus(AppStorageStatus_t *status);

//Camera任务查询当前是否需要存储帧，不取得Media所有权
bool App_StorageWantsFrame(uint32_t now);

//Camera借出原帧给SD，归还确认前不能复用
SysResult_t App_StorageOfferFrame(const CameraFrame_t *frame, const AppFrameMeta_t *meta);

//只有Storage任务调用，处理有界命令和租借帧
void App_StorageProcess(void);

//Camera归还SD引用，Storage任务不得直接操作Media
void App_StorageProcessReturns(void);
#endif
