#include "../../CM7/ApplicationV2/Modules/Storage/storage_backend.h"
#include "../../CM7/Platform/sd_block.c"
#include "jpeg_frame.h"
#include <string.h>

uint32_t g_failure;
#define CHECK(x) do { if (!(x)) { g_failure = __LINE__; return 1; } } while (0)
#define CARD_SECTORS 8192U
#define SECTOR_BYTES 512U
static uint8_t *const card = (uint8_t *)0xD0000000U;
static BspSdmmcIoDiagnostics_t io;
static VStorageFS volume;
static uint32_t write_blocks, drop_write, recovery, owner;
static const uint8_t jpeg[] = {0xff, 0xd8, 0xff, 0xc0, 0, 8, 8, 0, 240, 1, 64, 1,
                              0xff, 0xda, 0, 2, 0x34, 0xff, 0xd9};

static void init_image(void)
{
    /* 最小FAT16 BPB，扇区镜像由模拟器清零，FatFS按真实格式更新卷。 */
    static const uint8_t bpb[] = {
        0xeb, 0x3c, 0x90, 'M','S','D','O','S','5','.','0',
        0,2,1,1,0,2,0,2,0,32,0xf8,32,0,32,0,64,0
    };
    memcpy(card, bpb, sizeof(bpb));
    card[38] = 0x29U;
    memcpy(card + 43, "NO NAME    FAT16   ", 19U);
    card[510] = 0x55U;
    card[511] = 0xaaU;
    card[1U * SECTOR_BYTES] = card[33U * SECTOR_BYTES] = 0xf8U;
    memset(card + 1U * SECTOR_BYTES + 1, 0xff, 3);
    memset(card + 33U * SECTOR_BYTES + 1, 0xff, 3);
}

static int native_owner(void *p)
{
    (void)p;
    return owner == 1U;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (TaskHandle_t)(uintptr_t)owner;
}

BaseType_t xTaskGetSchedulerState(void)
{
    return taskSCHEDULER_RUNNING;
}

QueueHandle_t xQueueGenericCreateStatic(UBaseType_t a, UBaseType_t b, uint8_t *storage,
                                       StaticQueue_t *queue, uint8_t kind)
{
    (void)a; (void)b; (void)storage; (void)kind;
    return (QueueHandle_t)queue;
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t queue, TickType_t timeout)
{
    (void)queue; (void)timeout;
    return pdFALSE;
}

BaseType_t xQueueGiveFromISR(QueueHandle_t queue, BaseType_t *wake)
{
    (void)queue; (void)wake;
    return pdFALSE;
}

SysResult_t BSP_SDMMC_SetWaitOps(const BspSdmmcWaitOps_t *ops)
{
    return ops && ops->can_wait && ops->wait && ops->notify ? SYS_OK : SYS_ERR_INVALID_ARG;
}

SysResult_t BSP_SDMMC_ReadDetect(uint32_t *raw, bool *present)
{
    *raw = 0;
    *present = true;
    return SYS_OK;
}

SysResult_t BSP_SDMMC_Init(void)
{
    return io.transfer_ready ? SYS_ERR_ALREADY_EXISTS : SYS_OK;
}

SysResult_t BSP_SDMMC_EnableTransferMode(void)
{
    io.transfer_ready = 1;
    return SYS_OK;
}

SysResult_t BSP_SDMMC_GetIoDiagnostics(BspSdmmcIoDiagnostics_t *d)
{
    *d = io;
    return SYS_OK;
}

SysResult_t BSP_SDMMC_GetCardInfo(BspSdmmcCardInfo_t *d)
{
    memset(d, 0, sizeof(*d));
    d->logical_block_count = CARD_SECTORS;
    d->logical_block_size = SECTOR_BYTES;
    return SYS_OK;
}

SysResult_t BSP_SDMMC_GetIdentity(uint32_t cid[4])
{
    memset(cid, 0, 16);
    cid[0] = 0x1234U;
    return SYS_OK;
}

static int in_range(uint32_t lba, uint32_t count)
{
    return count && lba < CARD_SECTORS && count <= CARD_SECTORS - lba && !io.fault;
}

SysResult_t BSP_SDMMC_ReadBlocks(uint32_t lba, uint32_t count, void *buffer, uint32_t timeout)
{
    (void)timeout;
    if (!in_range(lba, count))
        return SYS_ERR_IO;
    memcpy(buffer, card + lba * SECTOR_BYTES, count * SECTOR_BYTES);
    return SYS_OK;
}

SysResult_t BSP_SDMMC_WriteBlocks(uint32_t lba, uint32_t count, const void *buffer, uint32_t timeout)
{
    (void)timeout;
    if (!in_range(lba, count) || drop_write)
    {
        io.active = io.fault = 1U;
        return SYS_ERR_IO;
    }
    write_blocks += count;
    memcpy(card + lba * SECTOR_BYTES, buffer, count * SECTOR_BYTES);
    return SYS_OK;
}

SysResult_t BSP_SDMMC_PrepareWriteInput(void *destination, const void *source, uint32_t bytes)
{
    memcpy(destination, source, bytes);
    return SYS_OK;
}

SysResult_t BSP_SDMMC_Recover(void)
{
    recovery++;
    memset(&io, 0, sizeof(io));
    return SYS_OK;
}

int Storage_Native(uint32_t id)
{
    VStorageConfig config = {VST_AVI, 0, 65536, 4, {10, 1}, "0:/VID00001.AVI"};
    const CoreMediaFormat fmt = {1, 4, 2, 320, 240, 0, 50000, 50000, 1, 2, 2, 4, 1, 0};
    const MRView view = {jpeg, 7, jpeg + 7, sizeof(jpeg) - 7U};
    VStorageFacts facts;
    StorageFile_t check = 0;
    uint8_t contents[24];
    uint32_t got;
    const VStorageBackendOps *ops = &VStorageFS_Ops;
    if (id)
    {
        config.mode = VST_SNAPSHOT;
        memcpy(config.path, "0:/IMG00001.JPG", sizeof(config.path));
    }
    init_image();
    CHECK(SdBlock_Prepare() == SYS_OK);
    CHECK(StorageFS_Init() == SYS_OK);
    owner = 1;
    CHECK(VStorageFS_Init(&volume, native_owner, NULL) == SYS_OK);
    CHECK(ops->mount(&volume, &config) == SYS_OK && !write_blocks);
    CHECK(ops->open(&volume, &config, &fmt) == SYS_OK);
    if (id == 2)
    {
        uint32_t old_recovery = recovery;
        drop_write = 1;
        CHECK(ops->write(&volume, &config, &fmt, &view, 42U) == SYS_ERR_IO);
        CHECK(ops->observe(&volume, &facts) == SYS_OK);
        CHECK((facts.flags & VST_FS_FAULT) && !(facts.flags & VST_IO_QUIET));
        CHECK(ops->recover(&volume) == SYS_OK && recovery == old_recovery + 1U);
        CHECK(ops->observe(&volume, &facts) == SYS_OK);
        CHECK(facts.flags == (VST_VOLUME_RELEASED | VST_IO_QUIET));
        return 0;
    }
    CHECK(ops->write(&volume, &config, &fmt, &view, 42U) == SYS_OK);
    CHECK(ops->finish(&volume) == SYS_OK && ops->close(&volume) == SYS_OK);
    CHECK(ops->unmount(&volume) == SYS_OK && write_blocks);
    CHECK(ops->observe(&volume, &facts) == SYS_OK);
    CHECK(facts.flags == (VST_VOLUME_RELEASED | VST_IO_QUIET | VST_FILE_CLOSED | VST_FILE_FINISHED));
    CHECK(StorageFS_Mount(false, 0) == SYS_OK);
    CHECK(StorageFS_Open(config.path, STORAGE_FS_OPEN_READ, &check) == SYS_OK);
    CHECK(StorageFS_Read(check, contents, sizeof(contents), &got) == SYS_OK);
    CHECK(id == 0 ? (got == sizeof(contents) && !memcmp(contents, "RIFF", 4U)) :
                    (got == sizeof(jpeg) && !memcmp(contents, jpeg, sizeof(jpeg))));
    CHECK(StorageFS_Close(check) == SYS_OK && StorageFS_Unmount() == SYS_OK);
    return 0;
}
