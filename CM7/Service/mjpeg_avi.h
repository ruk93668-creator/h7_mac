#ifndef MJPEG_AVI_H
#define MJPEG_AVI_H
#include "storage_fs.h"
#include "camera_config.h"

#define MJPEG_AVI_MAX_FRAMES 4096U
#define MJPEG_AVI_INDEX_BASE 0xD0590000UL
#define MJPEG_AVI_INDEX_BYTES (MJPEG_AVI_MAX_FRAMES * 16U)

//单个录像文件只由StorageTask推进，不向其他任务借出文件句柄
typedef struct
{
    StorageFile_t file;
    uint32_t frames;
    uint32_t position;
    uint32_t first_tick;
    uint32_t last_tick;
    uint32_t rate;
    uint32_t scale;
    uint32_t max_frame;
    uint16_t width;
    uint16_t height;
    bool failed;
    bool finished;
} MjpegAvi_t;

//写入初始头部，文件必须是刚创建的空文件
SysResult_t MjpegAvi_Begin(MjpegAvi_t *avi, StorageFile_t file, uint16_t width, uint16_t height, uint16_t fps);

//追加完整JPEG，超出分段预算前拒绝，不写半个受限帧
SysResult_t MjpegAvi_Append(MjpegAvi_t *avi, const uint8_t *data, uint32_t size, uint32_t tick, uint32_t limit);

//写索引并回填头部、同步，不负责关闭应用持有的文件
//按FIFO两段依次写入同一Chunk，不分配整帧拼接缓冲
SysResult_t MjpegAvi_AppendFrame(MjpegAvi_t *avi, const CameraFrame_t *frame, uint32_t tick, uint32_t limit);

//封尾成功后仍由Storage负责关闭文件
SysResult_t MjpegAvi_Finish(MjpegAvi_t *avi);
#endif
