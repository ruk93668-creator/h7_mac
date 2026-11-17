#ifndef CM7_USB_MSC_SCSI_H
#define CM7_USB_MSC_SCSI_H
#include <stdint.h>
#include <stdbool.h>

//SCSI只生成命令计划，不访问USB、RTOS、HAL或文件系统
typedef enum
{
    MSC_SCSI_NONE = 0,
    MSC_SCSI_RESPONSE,
    MSC_SCSI_READ,
    MSC_SCSI_WRITE,
    MSC_SCSI_SYNC,
    MSC_SCSI_EJECT
} MscScsiOperation_t;

typedef struct
{
    uint32_t block_count;
    bool present;
    bool ready;
    bool write_protected;
} MscScsiMedia_t;

typedef struct
{
    uint8_t sense_key;
    uint8_t asc;
    uint8_t ascq;
    bool unit_attention;
    bool prevent;
    bool ejected;
} MscScsiContext_t;

typedef struct
{
    MscScsiOperation_t operation;
    uint32_t length;
    uint32_t lba;
    uint32_t blocks;
    uint8_t response[64];
    bool clear_sense;
    bool failed;
} MscScsiCommand_t;

void USB_MSC_ScsiSense(MscScsiContext_t *context, uint8_t key, uint8_t asc, uint8_t ascq);
bool USB_MSC_ScsiDecode(MscScsiContext_t *context, const MscScsiMedia_t *media,
    const uint8_t *cdb, uint8_t cdb_length, MscScsiCommand_t *command);
#endif
