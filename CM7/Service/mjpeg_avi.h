#ifndef MJPEG_AVI_H
#define MJPEG_AVI_H
#include "storage_fs.h"
#include "camera_config.h"

//单个AVI可登记的索引条目数
#define MJPEG_AVI_MAX_FRAMES 65536U
//外部SDRAM中静态录像索引表的首地址
#define MJPEG_AVI_INDEX_BASE 0xD0590000UL
//索引表总容量，每个条目十六字节
#define MJPEG_AVI_INDEX_BYTES (MJPEG_AVI_MAX_FRAMES * 16U)

//单个录像文件只由StorageTask推进，不向其他任务借出文件句柄
typedef struct
{
    //本次操作持有的文件句柄，关闭后不得继续使用
    StorageFile_t file;
    //已经成功追加到AVI的帧数
    uint32_t frames;
    //下一次追加的文件字节位置
    uint32_t position;
    //首个成功写入帧的时间戳，单位毫秒
    uint32_t first_tick;
    //最近成功写入帧的时间戳，单位毫秒
    uint32_t last_tick;
    //AVI帧率分子，收尾时根据实测时长计算
    uint32_t rate;
    //AVI帧率分母，与rate共同确定播放时基
    uint32_t scale;
    //已写入的最大图像块长度，单位字节
    uint32_t max_frame;
    //该图像或配置的水平像素数
    uint16_t width;
    //该图像或配置的垂直像素数
    uint16_t height;
    //已发生不可继续写入的故障，禁止伪造完整文件
    bool failed;
    //已写入末尾索引、回填文件头并完成同步的事实
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
