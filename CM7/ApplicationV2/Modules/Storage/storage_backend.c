#include "storage_backend.h"
#include "jpeg_frame.h"
#include "sd_block.h"
#include <string.h>

static int owner(const VStorageFS *fs)
{
    return fs && fs->is_owner && fs->is_owner(fs->owner);
}

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

static SysResult_t mount_volume(void *p, const VStorageConfig *c)
{
    VStorageFS *fs = p;
    SysResult_t z;
    if (!owner(fs) || fs->file || SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        return SYS_ERR_INVALID_STATE;
    fs->verified = 0U;
    fs->mode = c->mode;
    fs->facts = 0U;
    z = StorageFS_Mount(true, (uint8_t)c->partition);
    if (!SD_Block_IsOwner(SD_BLOCK_CLIENT_FATFS))
        fs->facts = VST_VOLUME_RELEASED;
    return z;
}

static SysResult_t open_file(void *p, const VStorageConfig *c, const CoreMediaFormat *f)
{
    VStorageFS *fs = p;
    SysResult_t z;
    if (!owner(fs) || fs->file)
        return SYS_ERR_INVALID_STATE;
    z = StorageFS_Open(c->path, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &fs->file);
    if (!z && c->mode == VST_AVI)
        z = MjpegAvi_Begin(&fs->avi, fs->file, (uint16_t)f->width, (uint16_t)f->height,
                           (uint16_t)c->sampling.num);
    return z;
}

static SysResult_t write_span(StorageFile_t file, const uint8_t *data, uint32_t bytes)
{
    uint32_t written;
    SysResult_t z;
    if (!bytes)
        return SYS_OK;
    z = StorageFS_Write(file, data, bytes, &written);
    return z ? z : written == bytes ? SYS_OK : SYS_ERR_NO_RESOURCE;
}

static SysResult_t close_file(void *p)
{
    VStorageFS *fs = p;
    SysResult_t z;
    if (!owner(fs) || !fs->file)
        return SYS_ERR_INVALID_STATE;
    z = StorageFS_Close(fs->file);
    if (!z)
    {
        fs->file = 0U;
        fs->facts |= VST_FILE_CLOSED;
    }
    return z;
}

static SysResult_t verify_snapshot(VStorageFS *fs, const VStorageConfig *c, const MRView *v)
{
    uint32_t expected = v->bytes + v->bytes2;
    uint32_t actual = 0U, cursor = 0U, read = 0U, crc = UINT32_MAX, original;
    SysResult_t z;
    original = JpegFrame_Crc32(v->data, v->bytes, UINT32_MAX);
    if (v->bytes2)
        original = JpegFrame_Crc32(v->data2, v->bytes2, original);
    z = StorageFS_Open(c->path, STORAGE_FS_OPEN_READ, &fs->file);
    if (!z)
    {
        fs->facts &= ~VST_FILE_CLOSED;
        z = StorageFS_GetSize(fs->file, &actual);
    }
    if (!z && actual != expected)
        z = SYS_ERR_PROTOCOL;
    while (!z && cursor < expected)
    {
        uint32_t n = expected - cursor;
        uint32_t first = cursor < v->bytes ? v->bytes - cursor : 0U;
        if (n > sizeof(fs->readback))
            n = sizeof(fs->readback);
        if (first > n)
            first = n;
        z = StorageFS_Read(fs->file, fs->readback, n, &read);
        if (!z && (read != n || (first && memcmp(fs->readback, v->data + cursor, first)) ||
                   (n > first && memcmp(fs->readback + first,
                                        v->data2 + (cursor + first - v->bytes), n - first))))
            z = SYS_ERR_PROTOCOL;
        if (!z)
            crc = JpegFrame_Crc32(fs->readback, n, crc);
        cursor += n;
    }
    if (!z && original != crc)
        z = SYS_ERR_PROTOCOL;
    return z;
}

static SysResult_t write_frame(void *p, const VStorageConfig *c, const CoreMediaFormat *f,
                               const MRView *v, uint32_t timestamp)
{
    VStorageFS *fs = p;
    CameraFrame_t frame;
    SysResult_t z;
    if (!owner(fs) || !fs->file || !v->data || !v->bytes || (v->bytes2 && !v->data2) ||
        v->bytes > UINT32_MAX - v->bytes2)
        return SYS_ERR_INVALID_STATE;
    frame.width = (uint16_t)f->width;
    frame.height = (uint16_t)f->height;
    frame.format = CAMERA_PIXEL_FORMAT_JPEG;
    frame.data = v->data;
    frame.size_bytes = v->bytes + v->bytes2;
    frame.data2 = v->data2;
    frame.size_bytes2 = v->bytes2;
    if (frame.size_bytes > f->max_payload || frame.size_bytes > c->file_limit)
        return SYS_ERR_INVALID_ARG;
    z = JpegFrame_Validate(&frame);
    if (z)
        return z;
    if (c->mode == VST_AVI)
        return MjpegAvi_AppendFrame(&fs->avi, &frame, timestamp, c->file_limit);
    z = write_span(fs->file, v->data, v->bytes);
    if (!z)
        z = write_span(fs->file, v->data2, v->bytes2);
    if (!z)
        z = StorageFS_Sync(fs->file);
    if (!z)
        z = close_file(fs);
    if (!z)
        z = verify_snapshot(fs, c, v);
    if (!z)
        fs->verified = 1U;
    return z;
}

static SysResult_t finish_file(void *p)
{
    VStorageFS *fs = p;
    SysResult_t z;
    if (!owner(fs) || !fs->file)
        return SYS_ERR_INVALID_STATE;
    z = fs->mode == VST_AVI ? MjpegAvi_Finish(&fs->avi)
                            : (fs->verified ? SYS_OK : SYS_ERR_INVALID_STATE);
    if (!z)
        fs->facts |= VST_FILE_FINISHED;
    return z;
}

static SysResult_t unmount_volume(void *p)
{
    VStorageFS *fs = p;
    SysResult_t z;
    if (!owner(fs) || fs->file)
        return SYS_ERR_INVALID_STATE;
    z = StorageFS_Unmount();
    if (!z)
        fs->facts |= VST_VOLUME_RELEASED;
    return z;
}

static SysResult_t recover_volume(void *p)
{
    VStorageFS *fs = p;
    SysResult_t z;
    if (!owner(fs))
        return SYS_ERR_INVALID_STATE;
    z = StorageFS_Recover();
    if (!z)
    {
        /* 凭据因恢复失效。没有成功Close/Finish的原记录仍保持失败。 */
        fs->file = 0U;
        fs->facts |= VST_VOLUME_RELEASED;
    }
    return z;
}

static SysResult_t observe(void *p, VStorageFacts *out)
{
    VStorageFS *fs = p;
    SdBlockStatus_t block;
    StorageFsDiagnostics_t diag;
    SysResult_t z;
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
    z = SD_Block_GetStatus(&block);
    if (z)
        return z;
    z = StorageFS_GetDiagnostics(&diag);
    if (z)
        return z;
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
