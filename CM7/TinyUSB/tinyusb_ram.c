/* Thin TinyUSB callback adapter: reuses the existing RAM block backend, not BOT. */
#include "tusb.h"
#include "usb_msc_backend.h"
#include "tinyusb_ram.h"
#include <string.h>
static MscBlockRequest_t s_request;
static bool s_pending;
static uint32_t s_token;
static const MscBackendOps_t *Ram(void) { return USB_MSC_RamBackend(); }
bool TinyUSB_RamDrained(void) { return !s_pending && Ram()->drained(1U); }
void TinyUSB_RamPoll(void)
{
    MscBlockCompletion_t completion;
    if (!s_pending) return;
    if (Ram()->poll(&s_request, &completion) == SYS_OK)
    {
        /* The backend no longer owns the buffer before completion is queued. */
        int32_t bytes = completion.result == SYS_OK && completion.completed_blocks == s_request.blocks ?
            (int32_t)(completion.completed_blocks * 512U) : TUD_MSC_RET_ERROR;
        s_pending = false;
        (void)tud_msc_async_io_done(bytes, false);
    }
}
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t rev[4])
{ (void)lun; memcpy(vendor,"H7 TEST ",8); memcpy(product,"TinyUSB RAM LUN ",16); memcpy(rev,"0001",4); }
bool tud_msc_test_unit_ready_cb(uint8_t lun) { return lun == 0U; }
void tud_msc_capacity_cb(uint8_t lun, uint32_t *blocks, uint16_t *size)
{ MscScsiMedia_t m; uint32_t generation; (void)lun; Ram()->media(&m,&generation); *blocks=m.block_count; *size=512U; }
bool tud_msc_is_writable_cb(uint8_t lun) { return lun == 0U; }
static int32_t Submit(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size, bool write)
{
    if (lun != 0U || s_pending || offset % 512U != 0U || size == 0U || size % 512U != 0U ||
        lba > UINT32_MAX - offset / 512U || s_token == UINT32_MAX) return TUD_MSC_RET_ERROR;
    memset(&s_request,0,sizeof(s_request));
    s_request.epoch=1U; s_request.token=++s_token; s_request.generation=1U;
    s_request.operation=write ? MSC_SCSI_WRITE : MSC_SCSI_READ;
    s_request.lba=lba + offset/512U; s_request.blocks=size/512U; s_request.buffer=buffer;
    if (Ram()->submit(&s_request) != SYS_OK) return TUD_MSC_RET_ERROR;
    s_pending=true; return TUD_MSC_RET_ASYNC;
}
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{ return Submit(lun,lba,offset,buffer,size,false); }
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{ return Submit(lun,lba,offset,buffer,size,true); }
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const cmd[16], void *buffer, uint16_t size)
{
    (void)cmd; (void)buffer; (void)size;
    tud_msc_set_sense(lun,SCSI_SENSE_ILLEGAL_REQUEST,0x20,0); return TUD_MSC_RET_ERROR;
}
