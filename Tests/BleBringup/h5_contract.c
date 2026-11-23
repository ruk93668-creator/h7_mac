#include "bt_hci_h5.h"
#include <string.h>
#define C(x) do{if(!(x))return __LINE__;}while(0)
static BtH5_t h;static unsigned received;
static const uint8_t sync[]={192,0,47,0,208,2,125,192};
static const uint8_t conf[]={192,0,63,0,219,220,4,123,219,222,192};
static const uint8_t ack[]={192,8,0,0,247,192};
static const uint8_t event[]={192,219,220,100,0,219,221,14,4,1,0,0,0,55,211,192};
static const uint8_t sync_request[] = {0xc0, 0x00, 0x2f, 0x00, 0xd0, 0x01, 0x7e, 0xc0};
static uint8_t sent_sync[30U * sizeof(sync_request)];
static uint32_t sent_sync_bytes;

static SysResult_t Write(void *ctx, const uint8_t *bytes, uint32_t length, uint32_t *count)
{
    *count = length > 3U ? 3U : length;
    if (ctx != NULL)
    {
        if (sent_sync_bytes + *count > sizeof(sent_sync))
            return SYS_ERR_NO_RESOURCE;
        memcpy(sent_sync + sent_sync_bytes, bytes, *count);
        sent_sync_bytes += *count;
    }
    return *count == length ? SYS_OK : SYS_ERR_BUSY;
}
static SysResult_t Receive(void *ctx,uint8_t t,const uint8_t *b,uint32_t n){(void)ctx;(void)t;(void)b;(void)n;received++;return SYS_OK;}
uint32_t H5_Test(void){BtH5Ops_t ops={Write,Receive,0};unsigned i;uint8_t bad[sizeof(event)];const uint8_t cmd[]={3,12,0};
ops.context = sent_sync;
C(BtH5_Start(&h, &ops, 0) == SYS_OK);
for (i = 0; i < 3200; i += 10)
    BtH5_Poll(&h, i);
C(h.debug.state == BT_H5_FAILED && h.debug.result == SYS_ERR_TIMEOUT);
C(h.debug.link_attempts == 30U && h.debug.tx_frames == 30U);
C(sent_sync_bytes == sizeof(sent_sync));
for (i = 0; i < sent_sync_bytes; i += sizeof(sync_request))
    C(memcmp(sent_sync + i, sync_request, sizeof(sync_request)) == 0);
ops.context = NULL;
C(BtH5_Start(&h,&ops,0)==SYS_OK);C(BtH5_Feed(&h,sync,sizeof(sync))==SYS_OK);C(BtH5_Feed(&h,conf,sizeof(conf))==SYS_OK);C(h.debug.state==BT_H5_ACTIVE && h.crc);
C(BtH5_Feed(&h,event,sizeof(event))==SYS_OK && received==1);C(BtH5_Feed(&h,event,sizeof(event))==SYS_OK && received==1 && h.debug.duplicates==1);
memcpy(bad,event,sizeof(event));bad[sizeof(event)-2]^=1;C(BtH5_Feed(&h,bad,sizeof(bad))==SYS_OK && h.debug.crc_errors==1);
C(BtH5_Send(&h,1,cmd,3)==SYS_OK);for(i=0;i<20;i++)C(BtH5_Poll(&h,0)==SYS_OK);C(h.sent);C(BtH5_Feed(&h,ack,sizeof(ack))==SYS_OK && !h.awaiting && h.tx_seq==1);
C(BtH5_Send(&h,1,cmd,3)==SYS_OK);for(i=0;i<2500;i+=10)BtH5_Poll(&h,i);C(h.debug.state==BT_H5_FAILED && h.debug.retransmits==3);return 0;}
