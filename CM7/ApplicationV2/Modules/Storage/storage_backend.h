// 通过文件系统端口处理录像、抓拍、封尾和卷收尾。

#ifndef V2_STORAGE_BACKEND_H
#define V2_STORAGE_BACKEND_H

#include "storage_format.h"
#include "../../Resource/media_source.h"
#include "mjpeg_avi.h"

#define VST_VOLUME_HELD 1U
#define VST_FILE_OPEN 2U
#define VST_FILE_FINISHED 4U
#define VST_FILE_CLOSED 8U
#define VST_VOLUME_RELEASED 16U
#define VST_IO_QUIET 32U
#define VST_FS_FAULT 64U

typedef struct
{
    uint32_t flags;
} VStorageFacts;

typedef struct
{
    /* 在Storage owner中调用。IO动作一次执行，Write的非OK不保证未写入。 */
    SysResult_t (*mount)(void *, const VStorageConfig *);
    SysResult_t (*open)(void *, const VStorageConfig *, const CoreMediaFormat *);
    SysResult_t (*write)(void *, const VStorageConfig *, const CoreMediaFormat *, const MRView *,
                         uint32_t timestamp);
    SysResult_t (*finish)(void *);
    SysResult_t (*close)(void *);
    SysResult_t (*unmount)(void *);
    SysResult_t (*recover)(void *);
    SysResult_t (*observe)(void *, VStorageFacts *);
} VStorageBackendOps;

/* 现有StorageFS、AVI唯一调用者。Authority仍在StorageFS/SD Block。 */
typedef struct
{
    int (*is_owner)(void *);
    void *owner;
    StorageFile_t file;
    MjpegAvi_t avi;
    uint32_t facts, mode, verified;
    uint8_t readback[256];
} VStorageFS;
// 绑定 Storage 卷与文件操作端口。
SysResult_t VStorageFS_Init(VStorageFS *, int (*is_owner)(void *), void *);
extern const VStorageBackendOps VStorageFS_Ops;
#endif
