#include "storage_fs.h"
#include "sd_block.h"
#include "mjpeg_avi.h"
#include <string.h>

#define DISK_SECTORS 2049U
#define SECTOR_BYTES 512U
#define DISK ((uint8_t *)0xD0200000UL)

volatile uint32_t g_failure, g_checks;
static SdBlockClient_t s_owner;
static bool s_initialized, s_sync_error;
static uint32_t s_writes;

#define CHECK(expr) do { ++g_checks; if (!(expr)) return g_failure = __LINE__; } while (0)

SysResult_t SD_Block_Claim(SdBlockClient_t client)
{
    if (s_owner != SD_BLOCK_CLIENT_NONE)
        return SYS_ERR_BUSY;
    s_owner = client;
    return SYS_OK;
}

SysResult_t SD_Block_Release(SdBlockClient_t client)
{
    if (s_owner != client)
        return SYS_ERR_INVALID_STATE;
    s_owner = SD_BLOCK_CLIENT_NONE;
    return SYS_OK;
}

bool SD_Block_IsOwner(SdBlockClient_t client)
{
    return s_owner == client;
}

SysResult_t SD_Block_GetStatus(SdBlockStatus_t *status)
{
    *status = (SdBlockStatus_t){true, s_initialized, false, false};
    return SYS_OK;
}

SysResult_t SD_Block_GetIdentity(uint32_t cid[4])
{
    cid[0] = 1U;
    return SYS_OK;
}

SysResult_t SD_Block_Sync(void)
{
    return s_sync_error ? SYS_ERR_IO : SYS_OK;
}

SysResult_t SD_Block_Init(void)
{
    s_initialized = true;
    return SYS_OK;
}

SysResult_t SD_Block_Recover(void)
{
    s_initialized = false;
    return SYS_OK;
}

SysResult_t SD_Block_GetInfo(BspSdmmcCardInfo_t *info)
{
    *info = (BspSdmmcCardInfo_t){.logical_block_count = DISK_SECTORS, .logical_block_size = SECTOR_BYTES};
    return SYS_OK;
}

SysResult_t SD_Block_Read(uint32_t lba, uint32_t count, void *data, uint32_t timeout)
{
    (void)timeout;
    if (lba >= DISK_SECTORS || count > DISK_SECTORS - lba)
        return SYS_ERR_INVALID_ARG;
    memcpy(data, DISK + lba * SECTOR_BYTES, count * SECTOR_BYTES);
    return SYS_OK;
}

SysResult_t SD_Block_Write(uint32_t lba, uint32_t count, const void *data, uint32_t timeout)
{
    (void)timeout;
    if (lba == 0U || lba >= DISK_SECTORS || count > DISK_SECTORS - lba)
        return SYS_ERR_INVALID_ARG;
    ++s_writes;
    memcpy(DISK + lba * SECTOR_BYTES, data, count * SECTOR_BYTES);
    return SYS_OK;
}

SysResult_t BSP_SDMMC_PrepareWriteInput(void *destination, const void *source, uint32_t bytes)
{
    if (destination == NULL || source == NULL || bytes == 0U || bytes > 8192U)
        return SYS_ERR_INVALID_ARG;
    memcpy(destination, source, bytes);
    return SYS_OK;
}

static void Test_Volume(void)
{
    uint8_t *partition = DISK + 446U + 2U * 16U;
    uint8_t *boot = DISK + SECTOR_BYTES;
    uint8_t *fat = boot + SECTOR_BYTES;
    partition[4] = 1U;
    partition[8] = 1U;
    partition[13] = 8U;
    DISK[510] = 0x55U;
    DISK[511] = 0xAAU;
    boot[0] = 0xEBU;
    boot[2] = 0x90U;
    boot[12] = 2U;
    boot[13] = 1U;
    boot[14] = 1U;
    boot[16] = 1U;
    boot[17] = 64U;
    boot[20] = 8U;
    boot[21] = 0xF8U;
    boot[22] = 6U;
    memcpy(boot + 54U, "FAT12   ", 8U);
    boot[510] = 0x55U;
    boot[511] = 0xAAU;
    fat[0] = 0xF8U;
    fat[1] = fat[2] = 0xFFU;
}

uint32_t StorageFsTest(uint32_t scenario)
{
    static const char path[] = "0:/00000001.AVI";
    static const uint8_t jpeg[] = {0xFFU, 0xD8U, 0xFFU, 0xD9U};
    MjpegAvi_t avi;
    StorageFile_t file, stale;
    StorageFsDiagnostics_t state;
    uint32_t rows, size;
    uint8_t data[288];

    Test_Volume();
    CHECK(StorageFS_Init() == SYS_OK);
    CHECK(s_owner == SD_BLOCK_CLIENT_NONE && s_writes == 0U);
    CHECK(StorageFS_Mount(false, 1U) == SYS_ERR_NOT_SUPPORTED);
    CHECK(s_owner == SD_BLOCK_CLIENT_NONE && s_writes == 0U);
    CHECK(StorageFS_Mount(false, 3U) == SYS_OK);
    CHECK(StorageFS_GetDiagnostics(&state) == SYS_OK && state.partition == 3U && !state.writable);
    CHECK(StorageFS_Open(path, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &file) != SYS_OK);
    CHECK(StorageFS_Unmount() == SYS_OK && s_writes == 0U);
    CHECK(StorageFS_Mount(true, 3U) == SYS_OK);
    CHECK(StorageFS_Open(path, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &file) == SYS_OK);
    CHECK(StorageFS_Unmount() == SYS_ERR_BUSY && s_owner == SD_BLOCK_CLIENT_FATFS);

    CHECK(MjpegAvi_Begin(&avi, file, 320U, 240U, 25U) == SYS_OK);
    CHECK(MjpegAvi_Append(&avi, jpeg, sizeof(jpeg), 0U, 256UL * 1024UL * 1024UL) == SYS_OK);
    CHECK(MjpegAvi_Append(&avi, jpeg, sizeof(jpeg), 40U, 256UL * 1024UL * 1024UL) == SYS_OK);
    if (scenario == 1U)
    {
        s_sync_error = true;
        CHECK(MjpegAvi_Finish(&avi) == SYS_ERR_IO && !avi.finished && avi.failed);
        CHECK(s_owner == SD_BLOCK_CLIENT_FATFS);
        CHECK(StorageFS_GetDiagnostics(&state) == SYS_OK && state.last_result == SYS_ERR_IO);
        s_sync_error = false;
        CHECK(StorageFS_Recover() == SYS_OK && s_owner == SD_BLOCK_CLIENT_NONE);
        return 0U;
    }

    CHECK(MjpegAvi_Finish(&avi) == SYS_OK && avi.finished);
    CHECK(StorageFS_Close(file) == SYS_OK);
    stale = file;
    CHECK(StorageFS_Unmount() == SYS_OK && s_owner == SD_BLOCK_CLIENT_NONE);
    CHECK(StorageFS_Mount(true, 3U) == SYS_OK);
    CHECK(StorageFS_Open(path, STORAGE_FS_OPEN_WRITE | STORAGE_FS_OPEN_CREATE_NEW, &file) == SYS_ERR_ALREADY_EXISTS);
    CHECK(StorageFS_Open(path, STORAGE_FS_OPEN_READ, &file) == SYS_OK && file != stale);
    CHECK(StorageFS_GetSize(stale, &size) != SYS_OK);
    CHECK(StorageFS_GetSize(file, &size) == SYS_OK && size == sizeof(data));
    CHECK(StorageFS_Read(file, data, sizeof(data), &rows) == SYS_OK && rows == sizeof(data));
    CHECK(memcmp(data, "RIFF", 4U) == 0 && memcmp(data + 220U, "movi", 4U) == 0);
    CHECK(memcmp(data + 248U, "idx1", 4U) == 0 && data[252] == 32U);
    CHECK(StorageFS_Close(file) == SYS_OK);
    CHECK(StorageFS_Unmount() == SYS_OK && s_owner == SD_BLOCK_CLIENT_NONE && s_writes != 0U);
    return 0U;
}
