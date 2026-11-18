// 通过文件系统端口处理录像、抓拍、封尾和卷收尾。

#include "storage_backend.h"
#include "jpeg_frame.h"
#include "sd_block.h"
#include <string.h>

// 校验状态修改操作运行在约定的所有者上下文。
static int owner(const VStorageFS *fs)
{
    return fs && fs->is_owner && fs->is_owner(fs->owner);
}

// 绑定 Storage 卷与文件操作端口。
SysResult_t VStorageFS_Init(VStorageFS *fs, int (*check)(void *), void *port)
{
    if (!fs || fs->is_owner || !check)
        return SYS_ERR_INVALID_ARG;
    if (!check(port))
        return SYS_ERR_INVALID_STATE;
    memset(fs, 0, sizeof(*fs));
    fs->is_owner = check;
    fs->owner = port;
    fs->facts = VST_VOLUME_RELEASED;
    return SYS_OK;
}

// 调用文件系统端口推进卷操作并保留真实结果。
static SysResult_t mount_volume(void *port_context, const VStorageConfig *config)
{
    VStorageFS *fs = port_context;
    SysResult_t result;
    if (!owner(fs) || fs->file || SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return SYS_ERR_INVALID_STATE;
    fs->verified = 0U;
    fs->mode = config->mode;
    fs->facts = 0U;
    result = StorageFS_Mount(true, (uint8_t)config->partition);
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        fs->facts = VST_VOLUME_RELEASED;
    return result;
}

// 在持有的卷上推进目标文件的打开或关闭。
static SysResult_t open_file(void *port_context, const VStorageConfig *config, const CoreMediaFormat *format)
{
    VStorageFS *fs = port_context;
    SysResult_t result;
    if (!owner(fs) || fs->file)
        return SYS_ERR_INVALID_STATE;
    result = StorageFS_Open(config->path, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &fs->file);
    if (!result && config->mode == VST_AVI)
        result = MjpegAvi_Begin(&fs->avi, fs->file, (uint16_t)format->width, (uint16_t)format->height,
                           (uint16_t)config->sampling.num);
    return result;
}

// 向文件写入一段媒体数据并检查实际结果。
static SysResult_t write_span(StorageFile_t file, const uint8_t *data, uint32_t bytes)
{
    uint32_t written;
    SysResult_t result;
    if (!bytes)
        return SYS_OK;
    result = StorageFS_Write(file, data, bytes, &written);
    return result ? result : written == bytes ? SYS_OK : SYS_ERR_NO_RESOURCE;
}

// 在持有的卷上推进目标文件的打开或关闭。
static SysResult_t close_file(void *port_context)
{
    VStorageFS *fs = port_context;
    SysResult_t result;
    if (!owner(fs) || !fs->file)
        return SYS_ERR_INVALID_STATE;
    result = StorageFS_Close(fs->file);
    if (!result)
    {
        fs->file = 0U;
        fs->facts |= VST_FILE_CLOSED;
    }
    return result;
}

// 验证抓拍文件输出是否满足快照契约。
static SysResult_t verify_snapshot(VStorageFS *fs, const VStorageConfig *config, const MRView *view)
{
    uint32_t expected = view->bytes + view->bytes2;
    uint32_t actual = 0U, cursor = 0U, read = 0U, crc = UINT32_MAX, original;
    SysResult_t result;
    original = JpegFrame_Crc32(view->data, view->bytes, UINT32_MAX);
    if (view->bytes2)
        original = JpegFrame_Crc32(view->data2, view->bytes2, original);
    result = StorageFS_Open(config->path, STORAGE_FS_OPEN_READ, &fs->file);
    if (!result)
    {
        fs->facts &= ~VST_FILE_CLOSED;
        result = StorageFS_GetSize(fs->file, &actual);
    }
    if (!result && actual != expected)
        result = SYS_ERR_PROTOCOL;
    while (!result && cursor < expected)
    {
        uint32_t chunk_bytes = expected - cursor;
        uint32_t first = cursor < view->bytes ? view->bytes - cursor : 0U;
        if (chunk_bytes > sizeof(fs->readback))
            chunk_bytes = sizeof(fs->readback);
        if (first > chunk_bytes)
            first = chunk_bytes;
        result = StorageFS_Read(fs->file, fs->readback, chunk_bytes, &read);
        if (!result && (read != chunk_bytes || (first && memcmp(fs->readback, view->data + cursor, first)) ||
                   (chunk_bytes > first && memcmp(fs->readback + first,
                                        view->data2 + (cursor + first - view->bytes), chunk_bytes - first))))
            result = SYS_ERR_PROTOCOL;
        if (!result)
            crc = JpegFrame_Crc32(fs->readback, chunk_bytes, crc);
        cursor += chunk_bytes;
    }
    if (!result && original != crc)
        result = SYS_ERR_PROTOCOL;
    return result;
}

// 写入当前帧的数据跨度，保留实际写入结果。
static SysResult_t write_frame(void *port_context, const VStorageConfig *config, const CoreMediaFormat *format,
                               const MRView *view, uint32_t timestamp)
{
    VStorageFS *fs = port_context;
    CameraFrame_t frame;
    SysResult_t result;
    if (!owner(fs) || !fs->file || !view->data || !view->bytes || (view->bytes2 && !view->data2) ||
        view->bytes > UINT32_MAX - view->bytes2)
        return SYS_ERR_INVALID_STATE;
    frame.width = (uint16_t)format->width;
    frame.height = (uint16_t)format->height;
    frame.format = CAMERA_PIXEL_FORMAT_JPEG;
    frame.data = view->data;
    frame.size_bytes = view->bytes + view->bytes2;
    frame.data2 = view->data2;
    frame.size_bytes2 = view->bytes2;
    if (frame.size_bytes > format->max_payload || frame.size_bytes > config->file_limit)
        return SYS_ERR_INVALID_ARG;
    result = JpegFrame_Validate(&frame);
    if (result)
        return result;
    if (config->mode == VST_AVI)
        return MjpegAvi_AppendFrame(&fs->avi, &frame, timestamp, config->file_limit);
    result = write_span(fs->file, view->data, view->bytes);
    if (!result)
        result = write_span(fs->file, view->data2, view->bytes2);
    if (!result)
        result = StorageFS_Sync(fs->file);
    if (!result)
        result = close_file(fs);
    if (!result)
        result = verify_snapshot(fs, config, view);
    if (!result)
        fs->verified = 1U;
    return result;
}

// 推进文件封尾及关闭，文件完成不证明媒体租约已归还。
static SysResult_t finish_file(void *port_context)
{
    VStorageFS *fs = port_context;
    SysResult_t result;
    if (!owner(fs) || !fs->file)
        return SYS_ERR_INVALID_STATE;
    result = fs->mode == VST_AVI ? MjpegAvi_Finish(&fs->avi)
                            : (fs->verified ? SYS_OK : SYS_ERR_INVALID_STATE);
    if (!result)
        fs->facts |= VST_FILE_FINISHED;
    return result;
}

// 调用文件系统端口推进卷操作并保留真实结果。
static SysResult_t unmount_volume(void *port_context)
{
    VStorageFS *fs = port_context;
    SysResult_t result;
    if (!owner(fs) || fs->file)
        return SYS_ERR_INVALID_STATE;
    result = StorageFS_Unmount();
    if (!result)
        fs->facts |= VST_VOLUME_RELEASED;
    return result;
}

// 按端口契约尝试恢复底层资源，保留原始故障。
static SysResult_t recover_volume(void *port_context)
{
    VStorageFS *fs = port_context;
    SysResult_t result;
    if (!owner(fs))
        return SYS_ERR_INVALID_STATE;
    result = StorageFS_Recover();
    if (!result)
    {
        /* 凭据因恢复失效。没有成功Close/Finish的原记录仍保持失败。 */
        fs->file = 0U;
        fs->facts |= VST_VOLUME_RELEASED;
    }
    return result;
}

// 刷新本轮可观察的底层运行和退出事实。
static SysResult_t observe(void *port_context, VStorageFacts *out)
{
    VStorageFS *fs = port_context;
    SdBlockStatus_t block;
    StorageFsDiagnostics_t diag;
    SysResult_t result;
    if (!owner(fs) || !out)
        return SYS_ERR_INVALID_STATE;
    out->flags = fs->facts | (fs->file ? VST_FILE_OPEN : 0U);
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
    {
        if (!(fs->facts & VST_VOLUME_RELEASED) || fs->file)
            return SYS_ERR_INVALID_STATE;
        out->flags |= VST_IO_QUIET;
        return SYS_OK;
    }
    out->flags |= VST_VOLUME_HELD;
    out->flags &= ~VST_VOLUME_RELEASED;
    result = SD_Block_GetStatus(&block);
    if (result)
        return result;
    result = StorageFS_GetDiagnostics(&diag);
    if (result)
        return result;
    /* FAULT可能仍持有传输。实际恢复前，保守地扣留媒体归还。 */
    if (!block.busy && !block.fault)
        out->flags |= VST_IO_QUIET;
    if (block.fault || diag.state == 3U)
        out->flags |= VST_FS_FAULT;
    return SYS_OK;
}

const VStorageBackendOps VStorageFS_Ops = {mount_volume,   open_file,  write_frame,
                                           finish_file,    close_file, unmount_volume,
                                           recover_volume, observe};
