//SCSI透明命令子集：显式字节序、范围和字段检查
#include "usb_msc_scsi.h"
#include <string.h>

static uint32_t Scsi_Read32(const uint8_t *data);
static void Scsi_Write32(uint8_t *data, uint32_t value);
static bool Scsi_Fail(MscScsiContext_t *context, MscScsiCommand_t *command,
    uint8_t key, uint8_t asc);
static bool Scsi_Ready(MscScsiContext_t *context, const MscScsiMedia_t *media,
    MscScsiCommand_t *command);

void USB_MSC_ScsiSense(MscScsiContext_t *context, uint8_t key, uint8_t asc, uint8_t ascq)
{
    context->sense_key = key;
    context->asc = asc;
    context->ascq = ascq;
}

//Decode不执行任何外部IO；长度与BOT主机意图由Transport独立核对
bool USB_MSC_ScsiDecode(MscScsiContext_t *context, const MscScsiMedia_t *media,
    const uint8_t *cdb, uint8_t cdb_length, MscScsiCommand_t *command)
{
    uint32_t length;
    uint8_t opcode;
    if (command == NULL)
        return false;
    memset(command, 0, sizeof(*command));
    if (context == NULL || media == NULL || cdb == NULL || cdb_length == 0U)
        return false;
    opcode = cdb[0];
    switch (opcode)
    {
        case 0x00U: case 0x03U: case 0x12U: case 0x1AU: case 0x1BU: case 0x1EU:
            if (cdb_length != 6U || cdb[5] != 0U)
                return Scsi_Fail(context, command, 5U, 0x24U);
            break;
        case 0x25U: case 0x28U: case 0x2AU: case 0x35U:
            if (cdb_length != 10U || cdb[9] != 0U)
                return Scsi_Fail(context, command, 5U, 0x24U);
            break;
        default:
            return Scsi_Fail(context, command, 5U, 0x20U);
    }
    if (context->unit_attention && opcode != 0x12U && opcode != 0x03U)
    {
        context->unit_attention = false;
        return Scsi_Fail(context, command, 6U, 0x28U);
    }
    switch (opcode)
    {
        case 0x12U:
            if (cdb[1] != 0U || cdb[2] != 0U || cdb[3] != 0U)
                return Scsi_Fail(context, command, 5U, 0x24U);
            command->response[0] = 0U;
            command->response[1] = 0x80U;
            command->response[2] = 4U;
            command->response[3] = 2U;
            command->response[4] = 31U;
            memcpy(command->response + 8U, "STM32   ", 8U);
            memcpy(command->response + 16U, "MSC Test LUN    ", 16U);
            memcpy(command->response + 32U, "0001", 4U);
            command->length = cdb[4] < 36U ? cdb[4] : 36U;
            command->operation = MSC_SCSI_RESPONSE;
            return true;
        case 0x03U:
            if (cdb[1] != 0U || cdb[2] != 0U || cdb[3] != 0U)
                return Scsi_Fail(context, command, 5U, 0x24U);
            command->response[0] = 0x70U;
            command->response[2] = context->sense_key;
            command->response[7] = 10U;
            command->response[12] = context->asc;
            command->response[13] = context->ascq;
            command->length = cdb[4] < 18U ? cdb[4] : 18U;
            command->operation = MSC_SCSI_RESPONSE;
            command->clear_sense = true;
            return true;
        case 0x00U:
            if (cdb[1] || cdb[2] || cdb[3] || cdb[4])
                return Scsi_Fail(context, command, 5U, 0x24U);
            return Scsi_Ready(context, media, command);
        case 0x25U:
            if (cdb[1] || cdb[2] || cdb[3] || cdb[4] || cdb[5] || cdb[6] || cdb[7] || cdb[8])
                return Scsi_Fail(context, command, 5U, 0x24U);
            if (!Scsi_Ready(context, media, command))
                return false;
            Scsi_Write32(command->response, media->block_count - 1U);
            Scsi_Write32(command->response + 4U, 512U);
            command->operation = MSC_SCSI_RESPONSE;
            command->length = 8U;
            return true;
        case 0x1AU:
            if ((cdb[1] & ~8U) != 0U || cdb[3] != 0U ||
                (cdb[2] != 0U && cdb[2] != 8U && cdb[2] != 0x3FU))
                return Scsi_Fail(context, command, 5U, 0x24U);
            length = cdb[2] == 0U ? 4U : 24U;
            command->response[0] = (uint8_t)(length - 1U);
            command->response[2] = media->write_protected ? 0x80U : 0U;
            if (length > 4U)
            {
                command->response[4] = 8U;
                command->response[5] = 18U;
            }
            command->length = cdb[4] < length ? cdb[4] : length;
            command->operation = MSC_SCSI_RESPONSE;
            return true;
        case 0x1EU:
            if (cdb[1] || cdb[2] || cdb[3] || cdb[4] > 1U)
                return Scsi_Fail(context, command, 5U, 0x24U);
            context->prevent = cdb[4] != 0U;
            return true;
        case 0x1BU:
            if (cdb[1] || cdb[2] || cdb[3] || (cdb[4] & ~3U))
                return Scsi_Fail(context, command, 5U, 0x24U);
            if (cdb[4] == 2U)
            {
                if (context->prevent)
                    return Scsi_Fail(context, command, 5U, 0x53U);
                command->operation = MSC_SCSI_EJECT;
            }
            else if ((cdb[4] & 1U) != 0U)
                context->ejected = false;
            else
                command->operation = MSC_SCSI_SYNC;
            return true;
        default:
            break;
    }
    if (cdb[1] || cdb[6])
        return Scsi_Fail(context, command, 5U, 0x24U);
    if (!Scsi_Ready(context, media, command))
        return false;
    command->lba = Scsi_Read32(cdb + 2U);
    command->blocks = ((uint32_t)cdb[7] << 8) | cdb[8];
    if (opcode == 0x35U)
    {
        if (command->lba >= media->block_count ||
            (command->blocks != 0U && command->blocks > media->block_count - command->lba))
            return Scsi_Fail(context, command, 5U, 0x21U);
        command->operation = MSC_SCSI_SYNC;
        return true;
    }
    if (command->blocks != 0U && (command->lba >= media->block_count ||
        command->blocks > media->block_count - command->lba))
        return Scsi_Fail(context, command, 5U, 0x21U);
    if (opcode == 0x2AU && media->write_protected)
        return Scsi_Fail(context, command, 7U, 0x27U);
    command->operation = opcode == 0x28U ? MSC_SCSI_READ : MSC_SCSI_WRITE;
    command->length = command->blocks * 512U;
    return true;
}

static uint32_t Scsi_Read32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | data[3];
}
static void Scsi_Write32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}
static bool Scsi_Fail(MscScsiContext_t *context, MscScsiCommand_t *command, uint8_t key, uint8_t asc)
{
    USB_MSC_ScsiSense(context, key, asc, 0U);
    command->failed = true;
    return false;
}
static bool Scsi_Ready(MscScsiContext_t *context, const MscScsiMedia_t *media, MscScsiCommand_t *command)
{
    if (!media->present || context->ejected)
        return Scsi_Fail(context, command, 2U, 0x3AU);
    if (!media->ready || media->block_count == 0U)
    {
        Scsi_Fail(context, command, 2U, 4U);
        context->ascq = 1U;
        return false;
    }
    return true;
}
