#include "bt_hci_h5.h"
#include <string.h>
#include <stddef.h>
#define H5_SYNC_MS 100U
#define H5_ACK_MS 500U
#define H5_SYNC_LIMIT 30U
#define H5_RETRY_LIMIT 3U
#define H5_CTRL_SYNC_RSP 1U
#define H5_CTRL_CONFIG_RSP 2U
#define H5_CTRL_WOKEN 4U

//H5 CRC采用反射0x1021多项式；线序为累加值位反转后的高字节先发送
static uint16_t H5_Crc(uint16_t crc, uint8_t value)
{
    uint32_t bit;
    crc ^= value;
    for (bit=0U;bit<8U;bit++) crc=(uint16_t)((crc>>1U)^((crc&1U)?0x8408U:0U));
    return crc;
}
static uint16_t H5_Reverse(uint16_t value)
{
    uint16_t result=0U; uint32_t bit;
    for(bit=0U;bit<16U;bit++) { result=(uint16_t)((result<<1U)|(value&1U)); value>>=1U; }
    return result;
}
static SysResult_t H5_Fail(BtH5_t *h, SysResult_t result)
{
    h->debug.state=BT_H5_FAILED; h->debug.result=result; return result;
}
static void H5_Escape(BtH5_t *h, uint8_t value)
{
    if(value==0xc0U || value==0xdbU || value==0x11U || value==0x13U)
    {
        h->encoded[h->encoded_length++]=0xdbU;
        h->encoded[h->encoded_length++]=value==0xc0U?0xdcU:value==0xdbU?0xddU:value==0x11U?0xdeU:0xdfU;
    }
    else h->encoded[h->encoded_length++]=value;
}
static void H5_Encode(BtH5_t *h,uint8_t type,const uint8_t *data,uint32_t length,bool reliable)
{
    uint8_t hdr[4]; uint16_t crc=0xffffU; uint32_t i;
    bool checksum=reliable && h->crc;
    hdr[0]=(uint8_t)((reliable?h->tx_seq:0U)|(h->rx_seq<<3U)|(reliable?0x80U:0U)|(checksum?0x40U:0U));
    hdr[1]=(uint8_t)(type|((length&15U)<<4U)); hdr[2]=(uint8_t)(length>>4U);
    hdr[3]=(uint8_t)~(hdr[0]+hdr[1]+hdr[2]);
    h->encoded_length=0U;h->encoded_offset=0U;h->encoded_reliable=reliable;
    h->encoded[h->encoded_length++]=0xc0U;
    for(i=0U;i<4U;i++) {crc=H5_Crc(crc,hdr[i]);H5_Escape(h,hdr[i]);}
    for(i=0U;i<length;i++) {crc=H5_Crc(crc,data[i]);H5_Escape(h,data[i]);}
    if(checksum) {crc=H5_Reverse(crc);H5_Escape(h,(uint8_t)(crc>>8U));H5_Escape(h,(uint8_t)crc);}
    h->encoded[h->encoded_length++]=0xc0U; h->ack_pending=false;
}
static void H5_LinkPacket(BtH5_t *h,const uint8_t *p,uint32_t n)
{
    if(n<2U) {h->debug.framing_errors++;return;}
    if(p[0]==1U && p[1]==0x7eU)
    {
        //控制器重启使所有旧命令失效；上层必须重新执行版本校验和初始化
        if(h->debug.state==BT_H5_ACTIVE) {h->debug.epoch++;H5_Fail(h,SYS_ERR_INVALID_STATE);return;}
        h->control|=H5_CTRL_SYNC_RSP;
    }
    else if(p[0]==2U && p[1]==0x7dU && h->debug.state==BT_H5_SYNC)
    {h->debug.state=BT_H5_CONFIG;h->debug.link_attempts=0U;h->link_tick=h->now-H5_SYNC_MS;}
    else if(p[0]==3U && p[1]==0xfcU)
    {
        h->config_reply=(uint8_t)(1U|((n>=3U && (p[2]&0x10U))?0x10U:0U));
        h->control|=H5_CTRL_CONFIG_RSP;
    }
    else if(p[0]==4U && p[1]==0x7bU && h->debug.state==BT_H5_CONFIG)
    {
        if(n>=3U && (p[2]&7U)==0U) {H5_Fail(h,SYS_ERR_PROTOCOL);return;}
        h->crc=n>=3U && (p[2]&0x10U)!=0U;
        h->debug.state=BT_H5_ACTIVE;h->debug.result=SYS_OK;
    }
    else if(p[0]==5U && p[1]==0xfaU) h->control|=H5_CTRL_WOKEN;
    else if(p[0]==7U && p[1]==0x78U) H5_Fail(h,SYS_ERR_NOT_SUPPORTED);
}
static void H5_Decode(BtH5_t *h)
{
    uint8_t *p=h->rx;uint32_t n=h->rx_length,len,i;uint8_t type,seq,ack;
    uint16_t crc=0xffffU;bool reliable,has_crc;SysResult_t result;
    if(n<4U || (uint8_t)(p[0]+p[1]+p[2]+p[3])!=0xffU) {h->debug.header_errors++;return;}
    len=(p[1]>>4U)|((uint32_t)p[2]<<4U);has_crc=(p[0]&0x40U)!=0U;reliable=(p[0]&0x80U)!=0U;
    if(len>BT_H5_MAX_PAYLOAD || n!=4U+len+(has_crc?2U:0U)) {h->debug.framing_errors++;return;}
    if(has_crc)
    {
        for(i=0U;i<4U+len;i++) crc=H5_Crc(crc,p[i]);
        crc=H5_Reverse(crc);
        if(p[4U+len]!=(uint8_t)(crc>>8U) || p[5U+len]!=(uint8_t)crc) {h->debug.crc_errors++;return;}
    }
    type=p[1]&15U;seq=p[0]&7U;ack=(p[0]>>3U)&7U;
    if(type==15U)
    {
        if(reliable || has_crc) {h->debug.framing_errors++;return;}
        H5_LinkPacket(h,p+4U,len);return;
    }
    if(h->debug.state!=BT_H5_ACTIVE) return;
    if((type==0U && (len!=0U || reliable)) || (type!=0U && type!=2U && type!=4U) ||
       ((type==2U || type==4U) && !reliable) || (reliable && h->crc && !has_crc))
    {h->debug.framing_errors++;return;}
    if(h->awaiting && h->sent)
    {
        if(ack==((h->tx_seq+1U)&7U)) {h->awaiting=false;h->sent=false;h->tx_seq=ack;}
        else if(ack!=h->tx_seq) h->debug.invalid_ack++;
    }
    else if(ack!=h->tx_seq) h->debug.invalid_ack++;
    if(type==0U) return;
    h->ack_pending=true;
    if(seq!=h->rx_seq)
    {
        if(seq==((h->rx_seq+7U)&7U)) h->debug.duplicates++;
        else h->debug.sequence_errors++;
        return;
    }
    result=h->ops.receive(h->ops.context,type,p+4U,len);
    if(result==SYS_ERR_BUSY || result==SYS_ERR_NO_RESOURCE) {h->debug.backpressure++;return;}
    if(result!=SYS_OK) {H5_Fail(h,result);return;}
    h->rx_seq=(h->rx_seq+1U)&7U;h->debug.rx_frames++;
}
SysResult_t BtH5_Start(BtH5_t *h,const BtH5Ops_t *ops,uint32_t now)
{
    if(h==NULL || ops==NULL || ops->write==NULL || ops->receive==NULL) return SYS_ERR_INVALID_ARG;
    memset(h,0,sizeof(*h));h->ops=*ops;h->now=now;h->link_tick=now-H5_SYNC_MS;
    h->debug.state=BT_H5_SYNC;h->debug.epoch=1U;return SYS_OK;
}
SysResult_t BtH5_Send(BtH5_t *h,uint8_t type,const uint8_t *p,uint32_t len)
{
    if(h==NULL || p==NULL || len==0U || len>BT_H5_MAX_PAYLOAD || (type!=1U && type!=2U)) return SYS_ERR_INVALID_ARG;
    if(h->debug.state!=BT_H5_ACTIVE) return SYS_ERR_INVALID_STATE;
    if(h->awaiting) return SYS_ERR_BUSY;
    memcpy(h->reliable,p,len);h->reliable_type=type;h->reliable_length=len;h->awaiting=true;h->sent=false;h->retries=0U;
    return SYS_OK;
}
SysResult_t BtH5_Feed(BtH5_t *h,const uint8_t *bytes,uint32_t length)
{
    uint32_t i;uint8_t b;
    if(h==NULL || (bytes==NULL && length!=0U)) return SYS_ERR_INVALID_ARG;
    if(h->debug.state==BT_H5_FAILED) return h->debug.result;
    for(i=0U;i<length;i++)
    {
        b=bytes[i];
        if(b==0xc0U)
        {
            if(h->in_frame && !h->dropping && h->rx_length!=0U)
            {if(h->escaped) h->debug.framing_errors++;else H5_Decode(h);}
            h->in_frame=true;h->escaped=false;h->dropping=false;h->rx_length=0U;
            if(h->debug.state==BT_H5_FAILED) return h->debug.result;
            continue;
        }
        if(!h->in_frame || h->dropping) continue;
        if(h->escaped)
        {
            h->escaped=false;
            if(b==0xdcU)b=0xc0U;else if(b==0xddU)b=0xdbU;else if(b==0xdeU)b=0x11U;else if(b==0xdfU)b=0x13U;
            else {h->dropping=true;h->debug.framing_errors++;continue;}
        }
        else if(b==0xdbU) {h->escaped=true;continue;}
        if(h->rx_length==sizeof(h->rx)) {h->dropping=true;h->debug.oversize++;continue;}
        h->rx[h->rx_length++]=b;
    }
    return SYS_OK;
}
SysResult_t BtH5_Poll(BtH5_t *h,uint32_t now)
{
    uint32_t count=0U;SysResult_t result;
    uint8_t p[3];uint32_t n=2U;
    if(h==NULL) return SYS_ERR_INVALID_ARG;
    h->now=now;
    if(h->debug.state==BT_H5_OFF) return SYS_ERR_INVALID_STATE;
    if(h->debug.state==BT_H5_FAILED) return h->debug.result;
    if(h->encoded_offset==h->encoded_length)
    {
        h->encoded_length=h->encoded_offset=0U;
        if(h->control!=0U)
        {
            if(h->control&H5_CTRL_SYNC_RSP) {p[0]=2U;p[1]=0x7dU;h->control&=(uint8_t)~H5_CTRL_SYNC_RSP;}
            else if(h->control&H5_CTRL_CONFIG_RSP) {p[0]=4U;p[1]=0x7bU;p[2]=h->config_reply;n=3U;h->control&=(uint8_t)~H5_CTRL_CONFIG_RSP;}
            else {p[0]=6U;p[1]=0xf9U;h->control&=(uint8_t)~H5_CTRL_WOKEN;}
            H5_Encode(h,15U,p,n,false);
        }
        else if(h->debug.state!=BT_H5_ACTIVE && (uint32_t)(now-h->link_tick)>=H5_SYNC_MS)
        {
            if(h->debug.link_attempts>=H5_SYNC_LIMIT) return H5_Fail(h,SYS_ERR_TIMEOUT);
            p[0]=h->debug.state==BT_H5_SYNC?1U:3U;p[1]=h->debug.state==BT_H5_SYNC?0x7eU:0xfcU;
            if(h->debug.state==BT_H5_CONFIG) {p[2]=0x11U;n=3U;}
            H5_Encode(h,15U,p,n,false);h->link_tick=now;h->debug.link_attempts++;
        }
        else if(h->ack_pending) H5_Encode(h,0U,NULL,0U,false);
        else if(h->awaiting && (!h->sent || (uint32_t)(now-h->ack_tick)>=H5_ACK_MS))
        {
            if(h->sent)
            {
                if(h->retries>=H5_RETRY_LIMIT) return H5_Fail(h,SYS_ERR_TIMEOUT);
                h->retries++;h->debug.retransmits++;
            }
            H5_Encode(h,h->reliable_type,h->reliable,h->reliable_length,true);
        }
    }
    if(h->encoded_length!=0U)
    {
        result=h->ops.write(h->ops.context,h->encoded+h->encoded_offset,h->encoded_length-h->encoded_offset,&count);
        if(count>h->encoded_length-h->encoded_offset) return H5_Fail(h,SYS_ERR_PROTOCOL);
        h->encoded_offset+=count;
        if(result!=SYS_OK && result!=SYS_ERR_BUSY) return H5_Fail(h,result);
        if(h->encoded_offset==h->encoded_length)
        {
            h->debug.tx_frames++;
            if(h->encoded_reliable && h->awaiting) {h->sent=true;h->ack_tick=now;}
        }
    }
    return SYS_OK;
}
