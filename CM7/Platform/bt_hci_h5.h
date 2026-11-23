#ifndef CM7_BT_HCI_H5_H
#define CM7_BT_HCI_H5_H
#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>
#define BT_H5_MAX_PAYLOAD 1024U
#define BT_H5_ENCODED_BYTES ((BT_H5_MAX_PAYLOAD + 6U) * 2U + 2U)
typedef enum { BT_H5_OFF, BT_H5_SYNC, BT_H5_CONFIG, BT_H5_ACTIVE, BT_H5_FAILED } BtH5State_t;
typedef struct
{
    SysResult_t (*write)(void *context, const uint8_t *bytes, uint32_t length, uint32_t *count);
    SysResult_t (*receive)(void *context, uint8_t type, const uint8_t *bytes, uint32_t length);
    void *context;
} BtH5Ops_t;
typedef struct
{
    uint32_t state, epoch, tx_frames, rx_frames, crc_errors, header_errors;
    uint32_t framing_errors, oversize, duplicates, sequence_errors, invalid_ack;
    uint32_t retransmits, backpressure, link_attempts;
    SysResult_t result;
} BtH5Debug_t;
typedef struct
{
    BtH5Ops_t ops;
    BtH5Debug_t debug;
    uint8_t rx[BT_H5_MAX_PAYLOAD+6U];
    uint8_t encoded[BT_H5_ENCODED_BYTES];
    uint8_t reliable[BT_H5_MAX_PAYLOAD];
    uint32_t rx_length, encoded_length, encoded_offset, reliable_length;
    uint32_t now, link_tick, ack_tick;
    uint8_t tx_seq, rx_seq, reliable_type, retries, control, config_reply;
    bool in_frame, escaped, dropping, awaiting, sent, ack_pending, crc, encoded_reliable;
} BtH5_t;
//单一线程拥有上下文；write允许BUSY伴随部分进展，receive须同步复制，不保留输入指针
SysResult_t BtH5_Start(BtH5_t *link, const BtH5Ops_t *ops, uint32_t now);
SysResult_t BtH5_Send(BtH5_t *link, uint8_t type, const uint8_t *payload, uint32_t length);
SysResult_t BtH5_Feed(BtH5_t *link, const uint8_t *bytes, uint32_t length);
SysResult_t BtH5_Poll(BtH5_t *link, uint32_t now);
#endif
