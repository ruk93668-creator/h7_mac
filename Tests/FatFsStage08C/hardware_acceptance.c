//独立实板验收入口，正常Keil工程不编译此文件
#include "hardware_acceptance.h"
#include "sd_block.h"
#include <string.h>

#define ACCEPT_BYTES 131109U
#define ACCEPT_GUARD 0x51A2D3E4UL

//CPU测试缓冲，IDMA仍只访问原32KiB中转区
typedef struct
{
    uint32_t before;
    uint8_t data[ACCEPT_BYTES];
    uint32_t after;
} AcceptanceBuffer_t;

//独立镜像的写入数据，不使用摄像头或SDRAM帧缓冲
static AcceptanceBuffer_t s_write __attribute__((section(".fatfs_acceptance"), zero_init, aligned(32)));

//独立镜像的读取数据，用于文件回读校验
static AcceptanceBuffer_t s_read __attribute__((section(".fatfs_acceptance"), zero_init, aligned(32)));

//记录用户命令与实板结果，不提供自动启动写测试
volatile FatFsAcceptance_t g_fatfs_acceptance;

//计算实际测试字节的CRC32
static uint32_t Acceptance_Crc(const uint8_t *data, uint32_t size);

//核对写入授权仍对应当前卡和当前卷
static bool Acceptance_Authorized(const StorageFsDiagnostics_t *volume, uint64_t capacity);

//由验收任务执行一个受控命令
void FatFsAcceptance_Process(uint32_t request)
{
    StorageFsDiagnostics_t volume;
    BspSdmmcCardInfo_t info;
    StorageFile_t file = 0U;
    SysResult_t result, cleanup = SYS_OK;
    uint32_t n, i, index;
    char path[] = "0:/F08C0001.TST";
    bool mounted = false;

    g_fatfs_acceptance.state = 1U;
    g_fatfs_acceptance.phase = 1U;
    g_fatfs_acceptance.bytes_written = 0U;
    g_fatfs_acceptance.bytes_read = 0U;
    g_fatfs_acceptance.buffer_valid = 0U;

    //恢复是单独命令，失败后不自动反复识卡或写卡
    if (request == 3U)
    {
        result = StorageFS_Recover();
        goto finished;
    }
    if (request != 1U && request != 2U)
    {
        result = SYS_ERR_INVALID_ARG;
        goto finished;
    }

    //首先只读挂载并记录真实卷和卡身份
    result = StorageFS_Mount(false, 0U);
    if (result != SYS_OK)
        goto finished;
    mounted = true;
    result = StorageFS_GetDiagnostics(&volume);
    if (result == SYS_OK)
        result = SD_Block_GetInfo(&info);
    if (result != SYS_OK)
        goto finish_volume;
    g_fatfs_acceptance.volume = volume;
    g_fatfs_acceptance.capacity = info.capacity_bytes;
    if (request == 1U)
        goto finish_volume;

    //用户需明确确认本轮卡及FAT卷，不继承之前原始LBA授权
    if (!Acceptance_Authorized(&volume, info.capacity_bytes))
    {
        result = SYS_ERR_INVALID_ARG;
        goto finish_volume;
    }
    result = StorageFS_Unmount();
    if (result != SYS_OK)
        goto finish_volume;
    mounted = false;
    result = StorageFS_Mount(true, 0U);
    if (result != SYS_OK)
        goto finished;
    mounted = true;
    result = StorageFS_GetDiagnostics(&volume);
    if (result == SYS_OK)
        result = SD_Block_GetInfo(&info);
    if (result != SYS_OK)
        goto finish_volume;
    if (!Acceptance_Authorized(&volume, info.capacity_bytes))
    {
        result = SYS_ERR_INVALID_ARG;
        goto finish_volume;
    }

    //生成唯一测试文件名，CREATE_NEW不会覆盖已有文件
    index = g_fatfs_acceptance.file_index;
    if (index == 0U || index > 9999U)
    {
        result = SYS_ERR_INVALID_ARG;
        goto finish_volume;
    }
    for (i = 0U; i < 4U; i++)
    {
        path[10U - i] = (char)('0' + index % 10U);
        index /= 10U;
    }
    s_write.before = s_write.after = ACCEPT_GUARD;
    s_read.before = s_read.after = ACCEPT_GUARD;
    for (i = 0U; i < ACCEPT_BYTES; i++)
        s_write.data[i] = (uint8_t)(i * 13U + 7U);
    memset(s_read.data, 0, sizeof(s_read.data));
    g_fatfs_acceptance.write_crc = Acceptance_Crc(s_write.data, ACCEPT_BYTES);
    g_fatfs_acceptance.phase = 2U;
    result = StorageFS_Open(path, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &file);
    if (result != SYS_OK)
        goto finish_volume;
    result = StorageFS_Write(file, s_write.data, ACCEPT_BYTES, &n);
    g_fatfs_acceptance.bytes_written = n;
    if (result != SYS_OK)
        goto finish_volume;
    g_fatfs_acceptance.phase = 3U;
    result = StorageFS_Sync(file);
    if (result == SYS_OK)
        result = StorageFS_Close(file);
    if (result != SYS_OK)
        goto finish_volume;
    file = 0U;

    //关闭后重新打开，不能只比较尚未同步的写缓存
    g_fatfs_acceptance.phase = 4U;
    result = StorageFS_Open(path, STORAGE_FS_OPEN_READ, &file);
    if (result == SYS_OK)
        result = StorageFS_Read(file, s_read.data, ACCEPT_BYTES, &n);
    if (result != SYS_OK)
        goto finish_volume;
    g_fatfs_acceptance.bytes_read = n;
    g_fatfs_acceptance.read_crc = Acceptance_Crc(s_read.data, n);
    g_fatfs_acceptance.buffer_valid = s_write.before == ACCEPT_GUARD &&
        s_write.after == ACCEPT_GUARD && s_read.before == ACCEPT_GUARD && s_read.after == ACCEPT_GUARD;
    if (n != ACCEPT_BYTES || g_fatfs_acceptance.read_crc != g_fatfs_acceptance.write_crc ||
        g_fatfs_acceptance.buffer_valid == 0U || memcmp(s_write.data, s_read.data, ACCEPT_BYTES) != 0)
        result = SYS_ERR_PROTOCOL;

finish_volume:
    //文件及卸载错误独立记录，不覆盖原始写入/同步错误
    if (mounted && StorageFS_GetDiagnostics(&volume) == SYS_OK)
    {
        g_fatfs_acceptance.volume = volume;
        if (volume.state == 2U)
        {
            if (file != 0U)
                cleanup = StorageFS_Close(file);
            if (cleanup == SYS_OK)
                cleanup = StorageFS_Unmount();
        }
        else
            cleanup = SYS_ERR_INVALID_STATE;
    }
finished:
    if (StorageFS_GetDiagnostics(&volume) == SYS_OK)
        g_fatfs_acceptance.volume = volume;
    g_fatfs_acceptance.result = result;
    g_fatfs_acceptance.cleanup_result = cleanup;
    g_fatfs_acceptance.state = result == SYS_OK && cleanup == SYS_OK ? 2U : 3U;
}

//计算CPU缓冲区CRC，不在ISR执行
static uint32_t Acceptance_Crc(const uint8_t *data, uint32_t size)
{
    uint32_t crc = UINT32_MAX, i, bit;
    for (i = 0U; i < size; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = (crc >> 1) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
    }
    return ~crc;
}

//检查实板写入授权与当前卷一致
static bool Acceptance_Authorized(const StorageFsDiagnostics_t *volume, uint64_t capacity)
{
    uint32_t i;
    if (g_fatfs_acceptance.write_confirm != FATFS_ACCEPT_WRITE_CONFIRM ||
        g_fatfs_acceptance.expected_first_lba != volume->first_lba ||
        g_fatfs_acceptance.expected_sector_count != volume->sector_count ||
        g_fatfs_acceptance.expected_capacity != capacity)
        return false;
    for (i = 0U; i < 4U; i++)
        if (g_fatfs_acceptance.expected_cid[i] != volume->cid[i])
            return false;
    return true;
}
