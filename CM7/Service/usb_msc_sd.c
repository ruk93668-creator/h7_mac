//受控SD测试设备：仅StorageTask使用SD_Block租约，不操作FatFS或HAL
#include "usb_msc_sd.h"
#include "sd_block.h"
#include "msc_test_config.h"
#include <string.h>
static uint32_t s_sd_generation = 1U;
static uint32_t s_sd_cid[4];
static BspSdmmcCardInfo_t s_sd_info;
static bool s_sd_locked;
static bool s_sd_recovery_attempted;
static bool s_sd_last_present;
static bool s_sd_claimed;

static SysResult_t MscSd_Init(void)
{
#if USB_MSC_SD_TEST
    SysResult_t result;
    //不释放既有FATFS租约，不尝试卸载或动态切换所有权
    result = SD_Block_Claim(SD_BLOCK_CLIENT_RAW);
    if (result != SYS_OK) return result;
    s_sd_claimed = true;
    result = SD_Block_Init();
    if (result == SYS_OK) result = SD_Block_GetInfo(&s_sd_info);
    if (result == SYS_OK && (s_sd_info.logical_block_size != 512U || s_sd_info.logical_block_count == 0U))
        result = SYS_ERR_NOT_SUPPORTED;
    if (result == SYS_OK) result = SD_Block_GetIdentity(s_sd_cid);
    s_sd_locked = result != SYS_OK;
    s_sd_last_present = result == SYS_OK;
    return result;
#else
    return SYS_ERR_NOT_SUPPORTED;
#endif
}
static void MscSd_Media(MscScsiMedia_t *media, uint32_t *generation)
{
    SdBlockStatus_t status;
    uint32_t cid[4];
    bool present = false;
    bool ready = false;
    if (s_sd_claimed && SD_Block_GetStatus(&status) == SYS_OK)
    {
        present = status.present;
        ready = status.initialized && !status.busy && !status.fault;
        if (ready && (SD_Block_GetIdentity(cid) != SYS_OK || memcmp(cid, s_sd_cid, sizeof(cid)) != 0))
            ready = false;
    }
    if (s_sd_last_present && (!present || !ready))
    {
        s_sd_locked = true;
        if (s_sd_generation != UINT32_MAX) ++s_sd_generation;
    }
    s_sd_last_present = present && ready;
    media->present = present;
    media->ready = ready && !s_sd_locked;
    media->write_protected = USB_MSC_SD_WRITE_ENABLE == 0;
    media->block_count = s_sd_info.logical_block_count;
    *generation = s_sd_generation;
}
static void MscSd_Execute(const MscBlockRequest_t *request, MscBlockCompletion_t *completion)
{
    MscScsiMedia_t media;
    uint32_t generation;
    BspSdmmcIoDiagnostics_t io;
    bool block_io = request->operation == MSC_SCSI_READ || request->operation == MSC_SCSI_WRITE;
    completion->result = SYS_ERR_INVALID_STATE;
    completion->completed_blocks = 0U;
    MscSd_Media(&media, &generation);
    if (!media.ready || generation != request->generation) return;
    if (block_io && (request->blocks == 0U || request->blocks > 2U || request->buffer == NULL ||
        request->lba >= media.block_count || request->blocks > media.block_count - request->lba))
    { completion->result = SYS_ERR_INVALID_ARG; return; }
    switch (request->operation)
    {
        case MSC_SCSI_READ:
            completion->result = SD_Block_Read(request->lba, request->blocks, request->buffer, 5000U);
            break;
        case MSC_SCSI_WRITE:
#if USB_MSC_SD_WRITE_ENABLE
            completion->result = SD_Block_Write(request->lba, request->blocks, request->buffer, 5000U);
            break;
#else
            completion->result = SYS_ERR_NOT_SUPPORTED;
            return;
#endif
        case MSC_SCSI_SYNC: case MSC_SCSI_EJECT:
            completion->result = SD_Block_Sync();
            break;
        default:
            completion->result = SYS_ERR_NOT_SUPPORTED;
            return;
    }
    if (completion->result == SYS_OK)
        completion->completed_blocks = block_io ? request->blocks : 0U;
    else
    {
        //第一次失败即锁定，读取本次匹配诊断后才允许恢复；不重试写入
        s_sd_locked = true;
        if (block_io && SD_Block_GetIoDiagnostics(&io) == SYS_OK &&
            io.lba == request->lba && io.requested_blocks == request->blocks &&
            io.write == (request->operation == MSC_SCSI_WRITE ? 1U : 0U) &&
            io.result == completion->result && io.completed_blocks <= request->blocks)
            completion->completed_blocks = io.completed_blocks;
    }
}
static bool MscSd_Quiescent(void)
{
    BspSdmmcIoDiagnostics_t io;
    if (SD_Block_GetIoDiagnostics(&io) != SYS_OK) return false;
    if (io.active == 0U) return true;
    //BSP故障可保持active；只尝试一次现有Recover，绝不以超时直接释放
    s_sd_locked = true;
    if (s_sd_recovery_attempted) return false;
    s_sd_recovery_attempted = true;
    if (SD_Block_Recover() != SYS_OK) return false;
    return SD_Block_GetIoDiagnostics(&io) == SYS_OK && io.active == 0U;
}
static const MscStorageDeviceOps_t s_sd_ops =
{ MscSd_Init, MscSd_Media, MscSd_Execute, MscSd_Quiescent };
const MscStorageDeviceOps_t *USB_MSC_SdDevice(void) { return &s_sd_ops; }
