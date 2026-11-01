//独立模块验收，不引入HAL、USB Core、Camera或Media
#include "usb_uvc_payload.h"
#include <stddef.h>
#include <string.h>

typedef struct
{
    uint32_t failed_line;
    uint32_t assertions;
    uint32_t actual;
    uint32_t expected;
} PayloadTestResult_t;

volatile PayloadTestResult_t g_result;
volatile uint32_t g_frame_watch;
volatile uint32_t g_frame_start;
volatile uint32_t g_frame_size;
volatile uint32_t g_trace_count;
volatile uint32_t g_lengths[256];
volatile uint32_t g_headers[256];
volatile uint32_t g_tokens[256];

//前后哨兵检查模块不会写出对象边界
static struct
{
    uint32_t before[4];
    UsbUvcPayload_t payload;
    uint32_t after[4];
} s_guard;

//独立软件模拟器中的图像区，末端之后不映射内存
#define FRAME_END 0x30020000UL

//写入独立于分包算法的图像字节序列
static uint8_t Pattern(uint32_t index);

//准备带边界的外部图像
static const uint8_t *PrepareFrame(uint32_t length, uint32_t skew);

//仅围绕真实Build开启模拟器越界读写监视
static SysResult_t Build(UsbUvcPayload_t *payload, UsbUvcPayloadPacket_t *packet);

//逐字节核对一整帧，每包确认前后检查偏移和FID
static int CheckFrame(uint32_t length, uint16_t packet_size, uint32_t skew);

static int Equal(uint32_t line, uint32_t actual, uint32_t expected)
{
    g_result.assertions++;
    if (actual != expected)
    {
        if (g_result.failed_line == 0U)
        { g_result.failed_line = line; g_result.actual = actual; g_result.expected = expected; }
        return 0;
    }
    return 1;
}
#define EQ(a,b) do { if (!Equal(__LINE__, (uint32_t)(a), (uint32_t)(b))) return 1; } while (0)

//无标准库链接时使用的基础内存函数，不含动态分配
void *memset(void *dest, int value, size_t size)
{
    volatile uint8_t *p = dest;
    while (size--) *p++ = (uint8_t)value;
    return dest;
}
int memcmp(const void *a, const void *b, size_t size)
{
    const uint8_t *p = a; const uint8_t *q = b;
    while (size--) { if (*p != *q) return *p - *q; p++; q++; }
    return 0;
}
void __aeabi_memclr4(void *p, size_t n) { volatile uint8_t *q=p; while(n--) *q++=0U; }
void __aeabi_memclr8(void *p, size_t n) { volatile uint8_t *q=p; while(n--) *q++=0U; }
void __aeabi_memclr(void *p, size_t n) { volatile uint8_t *q=p; while(n--) *q++=0U; }
void __aeabi_memset(void *p, size_t n, int v) { volatile uint8_t *q=p; while(n--) *q++=(uint8_t)v; }
void __aeabi_memcpy(void *dest, const void *source, size_t n)
{
    volatile uint8_t *p = dest; const volatile uint8_t *q = source;
    while (n--) *p++ = *q++;
}
void __aeabi_memcpy4(void *p, const void *q, size_t n) { __aeabi_memcpy(p, q, n); }

int Payload_TestRun(uint32_t id)
{
    UsbUvcPayload_t *p = &s_guard.payload;
    UsbUvcPayload_t saved;
    UsbUvcPayload_t other = {0};
    UsbUvcPayloadPacket_t packet;
    UsbUvcPayloadPacket_t next;
    UsbUvcPayloadPacket_t other_packet;
    const uint8_t *frame;
    uint32_t token;
    uint32_t count;
    uint32_t i;
    static const uint32_t lengths[] = {1U,509U,510U,511U,1020U,1021U,23U,1530U,17U,1021U};
    static const uint16_t sizes[] = {512U,512U,512U,512U,512U,512U,3U,511U,4U,512U};

    memset(&s_guard, 0, sizeof(s_guard));
    for (i=0U;i<4U;i++) { s_guard.before[i]=0x11223344UL; s_guard.after[i]=0x55667788UL; }
    EQ(USB_UVC_PayloadInit(p), SYS_OK);
    frame = PrepareFrame(1021U, 0U);

    if (id == 0U || id == 1U)
    {
        EQ(CheckFrame(38400U,512U,0U),0U); EQ(g_trace_count,76U);
        for(i=0U;i<75U;i++) EQ(g_lengths[i],512U);
        EQ(g_lengths[75],152U); EQ(p->fid,1U);
        if(id==1U)
        {
            EQ(CheckFrame(38400U,512U,0U),0U); EQ(g_trace_count,152U); EQ(p->fid,0U);
            EQ(g_headers[0]&1U,0U); EQ(g_headers[76]&1U,1U);
        }
    }
    else if(id>=2U && id<=11U) EQ(CheckFrame(lengths[id-2U],sizes[id-2U],id==11U?1U:0U),0U);
    else if(id==12U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK); EQ(Build(p,&packet),SYS_OK);
        saved=*p; EQ(Build(p,&next),SYS_OK); EQ(memcmp(p,&saved,sizeof(*p)),0);
        EQ(packet.data,next.data); EQ(packet.length,next.length); EQ(packet.token,next.token);
        EQ(p->frame_offset,0U); EQ(p->fid,0U); EQ(p->token_counter,1U);
    }
    else if(id==13U || id==14U || id==15U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK); EQ(Build(p,&packet),SYS_OK);
        token=packet.token;
        if(id!=15U) EQ(USB_UVC_PayloadCompletePacket(p,token),SYS_OK);
        if(id==14U) EQ(Build(p,&next),SYS_OK);
        saved=*p;
        EQ(USB_UVC_PayloadCompletePacket(p,id==15U?token+1U:token),id==13U?SYS_ERR_INVALID_STATE:SYS_ERR_NOT_FOUND);
        EQ(memcmp(p,&saved,sizeof(*p)),0);
        if(id==15U) { EQ(USB_UVC_PayloadCompletePacket(p,0U),SYS_ERR_INVALID_ARG); EQ(memcmp(p,&saved,sizeof(*p)),0); }
    }
    else if(id==16U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1U,512U),SYS_OK); EQ(Build(p,&packet),SYS_OK);
        EQ(packet.data[1],0x82U); EQ(p->fid,0U); EQ(p->frame_offset,0U);
        EQ(USB_UVC_PayloadCompletePacket(p,packet.token),SYS_OK); EQ(p->fid,1U); EQ(p->state,USB_UVC_PAYLOAD_IDLE);
    }
    else if(id==17U || id==22U)
    {
        if(id==17U) EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK);
        EQ(USB_UVC_PayloadCancel(p),SYS_OK); EQ(USB_UVC_PayloadCancel(p),SYS_OK);
        EQ(p->fid,0U); EQ(p->frame,NULL); EQ(p->state,USB_UVC_PAYLOAD_IDLE);
    }
    else if(id==18U || id==19U || id==20U || id==23U || id==25U || id==26U || id==28U || id==29U || id==56U || id==58U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,id==58U?1U:1021U,512U),SYS_OK); EQ(Build(p,&packet),SYS_OK);
        token=packet.token; saved=*p;
        if(id==28U) { EQ(USB_UVC_PayloadReset(p),SYS_ERR_BUSY); EQ(memcmp(p,&saved,sizeof(*p)),0); }
        else
        {
            EQ(USB_UVC_PayloadCancel(p),SYS_ERR_BUSY); EQ(p->state,USB_UVC_PAYLOAD_CANCELLING);
            EQ(p->fid,0U); EQ(p->frame_offset,0U); EQ(p->frame,frame);
            EQ(memcmp(p->packet,saved.packet,512U),0); saved=*p;
            if(id==23U)
            {
                EQ(USB_UVC_PayloadCancel(p),SYS_ERR_BUSY); EQ(USB_UVC_PayloadCancel(p),SYS_ERR_BUSY);
                EQ(memcmp(p,&saved,sizeof(*p)),0);
            }
            if(id==29U) { EQ(USB_UVC_PayloadReset(p),SYS_ERR_BUSY); EQ(memcmp(p,&saved,sizeof(*p)),0); }
            if(id==56U)
            {
                next.data=(const uint8_t *)0x1234U; next.length=123U; next.token=456U;
                EQ(Build(p,&next),SYS_ERR_BUSY); EQ(next.data,0x1234U); EQ(next.length,123U); EQ(next.token,456U);
                EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_ERR_BUSY); EQ(memcmp(p,&saved,sizeof(*p)),0);
            }
            if(id==25U) { EQ(USB_UVC_PayloadAbortPacket(p,token+1U),SYS_ERR_NOT_FOUND); EQ(memcmp(p,&saved,sizeof(*p)),0); }
            if(id==20U) EQ(USB_UVC_PayloadCompletePacket(p,token),SYS_OK);
            else EQ(USB_UVC_PayloadAbortPacket(p,token),SYS_OK);
            EQ(p->fid,1U); EQ(p->state,USB_UVC_PAYLOAD_IDLE); EQ(p->frame,NULL); EQ(p->pending_token,0U);
            saved=*p; EQ(USB_UVC_PayloadAbortPacket(p,token),SYS_ERR_INVALID_STATE);
            EQ(USB_UVC_PayloadCompletePacket(p,token),SYS_ERR_INVALID_STATE); EQ(memcmp(p,&saved,sizeof(*p)),0);
            if(id==26U || id==58U)
            {
                EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK); EQ(Build(p,&next),SYS_OK);
                EQ(next.data[1]&1U,1U); EQ(next.token>token,true); saved=*p;
                EQ(USB_UVC_PayloadCompletePacket(p,token),SYS_ERR_NOT_FOUND); EQ(memcmp(p,&saved,sizeof(*p)),0);
            }
        }
    }
    else if(id==21U || id==27U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK);
        if(id==21U)
        {
            EQ(Build(p,&packet),SYS_OK); EQ(USB_UVC_PayloadCompletePacket(p,packet.token),SYS_OK);
            EQ(p->frame_offset,510U); EQ(p->fid,0U); EQ(USB_UVC_PayloadCancel(p),SYS_OK); EQ(p->fid,1U);
        }
        else { saved=*p; EQ(USB_UVC_PayloadReset(p),SYS_ERR_BUSY); EQ(memcmp(p,&saved,sizeof(*p)),0); }
    }
    else if(id==24U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK); EQ(Build(p,&packet),SYS_OK); saved=*p;
        EQ(USB_UVC_PayloadAbortPacket(p,packet.token),SYS_ERR_INVALID_STATE); EQ(memcmp(p,&saved,sizeof(*p)),0);
    }
    else if(id==30U || id==31U)
    {
        EQ(CheckFrame(1U,512U,0U),0U); token=p->token_counter; EQ(p->fid,1U);
        EQ(USB_UVC_PayloadReset(p),SYS_OK); EQ(p->fid,0U); EQ(p->token_counter,token);
        if(id==31U)
        {
            frame=PrepareFrame(1021U,0U); EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK);
            EQ(Build(p,&next),SYS_OK); saved=*p;
            EQ(USB_UVC_PayloadCompletePacket(p,token),SYS_ERR_NOT_FOUND); EQ(memcmp(p,&saved,sizeof(*p)),0);
        }
    }
    else if(id>=32U && id<=41U)
    {
        static const uint16_t bad_sizes[]={0U,1U,2U,513U,65535U};
        const uint8_t *source=frame; uint32_t length=1021U; uint16_t size=512U;
        if(id==32U) length=0U;
        if(id==33U) source=NULL;
        if(id>=34U && id<=38U) size=bad_sizes[id-34U];
        if(id==39U) { source=(const uint8_t *)0xFFFFFFF0UL; length=32U; }
        if(id==40U) { source=(const uint8_t *)p; length=sizeof(*p); }
        if(id==41U) { source=p->packet; length=1U; }
        saved=*p; EQ(USB_UVC_PayloadStartFrame(p,source,length,size),SYS_ERR_INVALID_ARG); EQ(memcmp(p,&saved,sizeof(*p)),0);
    }
    else if(id>=42U && id<=44U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK); saved=*p;
        EQ(Build(p,id==42U?NULL:id==43U?(UsbUvcPayloadPacket_t *)p->packet:(UsbUvcPayloadPacket_t *)frame),SYS_ERR_INVALID_ARG);
        EQ(memcmp(p,&saved,sizeof(*p)),0);
    }
    else if(id==45U || id==46U)
    {
        saved=*p;
        if(id==45U) EQ(Build(p,&packet),SYS_ERR_INVALID_STATE);
        else EQ(USB_UVC_PayloadCompletePacket(p,1U),SYS_ERR_INVALID_STATE);
        EQ(memcmp(p,&saved,sizeof(*p)),0);
    }
    else if(id==47U) EQ(USB_UVC_PayloadInit(NULL),SYS_ERR_INVALID_ARG);
    else if(id==48U)
    {
        EQ(USB_UVC_PayloadReset(NULL),SYS_ERR_INVALID_ARG);
        EQ(USB_UVC_PayloadStartFrame(NULL,frame,1U,512U),SYS_ERR_INVALID_ARG);
        EQ(USB_UVC_PayloadBuildPacket(NULL,&packet),SYS_ERR_INVALID_ARG);
        EQ(USB_UVC_PayloadCompletePacket(NULL,1U),SYS_ERR_INVALID_ARG);
        EQ(USB_UVC_PayloadCancel(NULL),SYS_ERR_INVALID_ARG);
        EQ(USB_UVC_PayloadAbortPacket(NULL,1U),SYS_ERR_INVALID_ARG);
    }
    else if(id==49U)
    {
        EQ(USB_UVC_PayloadReset(&other),SYS_ERR_INVALID_STATE);
        EQ(USB_UVC_PayloadStartFrame(&other,frame,1U,512U),SYS_ERR_INVALID_STATE);
        EQ(USB_UVC_PayloadBuildPacket(&other,&packet),SYS_ERR_INVALID_STATE);
        EQ(USB_UVC_PayloadCompletePacket(&other,1U),SYS_ERR_INVALID_STATE);
        EQ(USB_UVC_PayloadCancel(&other),SYS_ERR_INVALID_STATE);
        EQ(USB_UVC_PayloadAbortPacket(&other,1U),SYS_ERR_INVALID_STATE);
    }
    else if(id==50U || id==51U)
    {
        if(id==51U) { EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK); EQ(Build(p,&packet),SYS_OK); }
        saved=*p; EQ(USB_UVC_PayloadInit(p),SYS_ERR_ALREADY_EXISTS); EQ(memcmp(p,&saved,sizeof(*p)),0);
    }
    else if(id==52U || id==53U)
    {
        //仅用测试注入模拟数十亿次生命周期，不允许真实计数回绕
        p->token_counter=id==52U?UINT32_MAX-1U:UINT32_MAX;
        EQ(USB_UVC_PayloadStartFrame(p,frame,1U,512U),SYS_OK);
        if(id==52U)
        {
            EQ(Build(p,&packet),SYS_OK); EQ(packet.token,UINT32_MAX);
            EQ(USB_UVC_PayloadCompletePacket(p,packet.token),SYS_OK); EQ(p->fid,1U);
            EQ(USB_UVC_PayloadStartFrame(p,frame,1U,512U),SYS_OK);
        }
        saved=*p; EQ(Build(p,&next),SYS_ERR_NO_RESOURCE); EQ(memcmp(p,&saved,sizeof(*p)),0);
        EQ(USB_UVC_PayloadCancel(p),SYS_OK); EQ(USB_UVC_PayloadReset(p),SYS_OK);
        EQ(p->token_counter,UINT32_MAX);
    }
    else if(id==54U)
    {
        EQ(USB_UVC_PayloadInit(&other),SYS_OK); EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK);
        EQ(USB_UVC_PayloadStartFrame(&other,frame,1021U,511U),SYS_OK);
        EQ(Build(p,&packet),SYS_OK); EQ(Build(&other,&other_packet),SYS_OK); EQ(packet.data!=other_packet.data,true);
        EQ(USB_UVC_PayloadCompletePacket(p,packet.token),SYS_OK); EQ(p->frame_offset,510U); EQ(other.frame_offset,0U);
        EQ(USB_UVC_PayloadCancel(&other),SYS_ERR_BUSY); EQ(USB_UVC_PayloadAbortPacket(&other,other_packet.token),SYS_OK);
        EQ(other.fid,1U); EQ(p->fid,0U);
    }
    else if(id==55U || id==57U) EQ(CheckFrame(1021U,511U,id==55U?3U:0U),0U);
    else if(id==59U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK);
        for(i=0U;i<3U;i++)
        {
            saved=*p; EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_ERR_BUSY); EQ(memcmp(p,&saved,sizeof(*p)),0);
            if(i==0U) EQ(Build(p,&packet),SYS_OK);
            if(i==1U) EQ(USB_UVC_PayloadCancel(p),SYS_ERR_BUSY);
        }
    }
    else if(id==60U || id==61U)
    {
        EQ(USB_UVC_PayloadStartFrame(p,frame,1021U,512U),SYS_OK);
        if(id==60U) p->frame_offset=p->frame_length;
        else { EQ(Build(p,&packet),SYS_OK); p->pending_data_length=1022U; }
        saved=*p; EQ(Build(p,&next),SYS_ERR_INVALID_STATE); EQ(memcmp(p,&saved,sizeof(*p)),0);
    }
    else EQ(0U,1U);
    count=g_trace_count; EQ(count<=256U,true);
    for(i=0U;i<4U;i++) { EQ(s_guard.before[i],0x11223344UL); EQ(s_guard.after[i],0x55667788UL); }
    return 0;
}

//内部功能函数实现

static uint8_t Pattern(uint32_t index)
{
    return (uint8_t)(index*17U+(index>>8)*3U+5U);
}

static const uint8_t *PrepareFrame(uint32_t length, uint32_t skew)
{
    uint8_t *frame=(uint8_t *)(FRAME_END-length-skew);
    uint32_t i;
    g_frame_start=(uint32_t)frame; g_frame_size=length;
    for(i=0U;i<length;i++) frame[i]=Pattern(i);
    return frame;
}

static SysResult_t Build(UsbUvcPayload_t *payload, UsbUvcPayloadPacket_t *packet)
{
    SysResult_t result;
    g_frame_watch=1U;
    result=USB_UVC_PayloadBuildPacket(payload,packet);
    g_frame_watch=0U;
    return result;
}

static int CheckFrame(uint32_t length, uint16_t packet_size, uint32_t skew)
{
    UsbUvcPayload_t *p=&s_guard.payload;
    UsbUvcPayloadPacket_t packet;
    const uint8_t *frame=PrepareFrame(length,skew);
    uint32_t offset=0U;
    uint32_t count=0U;
    uint32_t eof_count=0U;
    uint32_t expected_count=(length-1U)/(packet_size-2U)+1U;
    uint32_t i;
    uint32_t data_length;
    uint32_t padded;
    uint8_t fid=p->fid;
    EQ(USB_UVC_PayloadStartFrame(p,frame,length,packet_size),SYS_OK);
    while(p->state!=USB_UVC_PAYLOAD_IDLE)
    {
        EQ(Build(p,&packet),SYS_OK); EQ(p->frame_offset,offset); EQ(p->fid,fid);
        EQ(packet.data[0],2U); EQ(packet.data[1]&0xFCU,0x80U); EQ(packet.data[1]&1U,fid);
        EQ(packet.length>2U && packet.length<=packet_size,true); EQ((uintptr_t)packet.data&3U,0U);
        data_length=packet.length-2U;
        EQ(data_length<=length-offset,true);
        EQ((packet.data[1]&2U)!=0U,offset+data_length==length);
        if(packet.data[1]&2U) eof_count++;
        for(i=0U;i<data_length;i++) { EQ(packet.data[i+2U],Pattern(offset+i)); EQ(frame[offset+i],Pattern(offset+i)); }
        padded=(packet.length+3U)&~3U;
        for(i=packet.length;i<padded;i++) EQ(packet.data[i],0U);
        if(g_trace_count<256U)
        {
            g_lengths[g_trace_count]=packet.length;
            g_headers[g_trace_count]=packet.data[1];
            g_tokens[g_trace_count]=packet.token;
            g_trace_count++;
        }
        offset+=data_length; count++;
        EQ(USB_UVC_PayloadCompletePacket(p,packet.token),SYS_OK);
        if(offset<length) { EQ(p->frame_offset,offset); EQ(p->fid,fid); }
    }
    EQ(offset,length); EQ(count,expected_count); EQ(eof_count,1U); EQ(p->fid,fid^1U);
    EQ(p->frame,NULL); EQ(p->pending_token,0U);
    return 0;
}
