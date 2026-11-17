//受控RAM LUN：16个512字节扇区，避免占用SDMMC专用区或溢出DTCM
#include "usb_msc_backend.h"
#include <string.h>

#define MSC_RAM_BLOCKS 16U
static uint8_t s_ram[MSC_RAM_BLOCKS * 512U] __attribute__((aligned(32)));
static MscBlockRequest_t s_ram_request;
static MscBlockCompletion_t s_ram_completion;
static bool s_ram_pending;

static void MscRam_Media(MscScsiMedia_t *media, uint32_t *generation)
{
    media->block_count = MSC_RAM_BLOCKS;
    media->present = true;
    media->ready = true;
    media->write_protected = false;
    *generation = 1U;
}
static SysResult_t MscRam_Submit(const MscBlockRequest_t *request)
{
    if (s_ram_pending)
        return SYS_ERR_BUSY;
    if (request == NULL || request->generation != 1U || request->token == 0U)
        return SYS_ERR_INVALID_ARG;
    if ((request->operation == MSC_SCSI_READ || request->operation == MSC_SCSI_WRITE) &&
        (request->buffer == NULL || request->blocks == 0U || request->lba >= MSC_RAM_BLOCKS ||
        request->blocks > MSC_RAM_BLOCKS - request->lba))
        return SYS_ERR_INVALID_ARG;
    if (request->operation != MSC_SCSI_READ && request->operation != MSC_SCSI_WRITE &&
        request->operation != MSC_SCSI_SYNC && request->operation != MSC_SCSI_EJECT)
        return SYS_ERR_NOT_SUPPORTED;
    s_ram_request = *request;
    s_ram_completion.result = SYS_OK;
    s_ram_completion.completed_blocks = request->blocks;
    if (request->operation == MSC_SCSI_READ)
        memcpy(request->buffer, s_ram + request->lba * 512U, request->blocks * 512U);
    if (request->operation == MSC_SCSI_WRITE)
        memcpy(s_ram + request->lba * 512U, request->buffer, request->blocks * 512U);
    s_ram_pending = true;
    return SYS_OK;
}
static SysResult_t MscRam_Poll(const MscBlockRequest_t *request, MscBlockCompletion_t *completion)
{
    if (!s_ram_pending || request->epoch != s_ram_request.epoch ||
        request->token != s_ram_request.token || request->generation != s_ram_request.generation)
        return SYS_ERR_INVALID_STATE;
    *completion = s_ram_completion;
    s_ram_pending = false;
    return SYS_OK;
}
static void MscRam_Cancel(uint32_t epoch)
{
    if (s_ram_pending && s_ram_request.epoch == epoch)
        s_ram_pending = false;
}
static bool MscRam_Drained(uint32_t epoch)
{
    return !s_ram_pending || s_ram_request.epoch != epoch;
}
static const MscBackendOps_t s_ram_ops =
{
    MscRam_Media, MscRam_Submit, MscRam_Poll, MscRam_Cancel, MscRam_Drained
};
const MscBackendOps_t *USB_MSC_RamBackend(void)
{
    return &s_ram_ops;
}
