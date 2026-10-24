//仅在本测试翻译单元访问正式文件的内部函数，不复制状态机算法
#include "../../CM7/Media/Src/camera_capture.c"
#include <string.h>

typedef struct { uint32_t fail_line,checks,operations,replaces,deferred,case_id; } TestResult;
volatile TestResult g_result;
static uint32_t mask,pending,inject_on_disable,pattern;
static CameraStream_t stream;
static CameraConfig_t config;
static uint32_t *targets[2];
static SysResult_t replace_result;
static uint32_t callback_error;
static int Complete(uint32_t slot);
static void Dispatch(void);
void *memcpy(void *d,const void *s,size_t n){uint8_t *p=d;const uint8_t *q=s;while(n--)*p++=*q++;return d;}
void *memset(void *d,int v,size_t n){uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
int memcmp(const void *a,const void *b,size_t n){const uint8_t *p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
uint32_t Platform_TimeGetMs(void){return 0;}
SysResult_t BSP_DCMI_StartSnapshot(uint32_t *p,uint32_t n){(void)p;(void)n;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_Stop(void){return SYS_ERR_NOT_SUPPORTED;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *e){(void)e;return false;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;if(inject_on_disable){pending=inject_on_disable;inject_on_disable=0;}Dispatch();}
void __set_PRIMASK(uint32_t value){mask=value;Dispatch();}
static int Eq(uint32_t line,uint32_t a,uint32_t b){g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Eq(__LINE__,(uint32_t)(a),(uint32_t)(b)))return 1;}while(0)
#define SAME(a,b) EQ(memcmp(&(a),&(b),sizeof(a)),0)

static SysResult_t Replace(BspDcmiTarget_t slot,uint32_t *buffer)
{
 uint32_t idx=slot==BSP_DCMI_TARGET_M0?stream.dma_m0_index:stream.dma_m1_index;
 g_result.replaces++;
 //切换回调发生时，正式状态机尚未提交软件映射与所有权
 if(idx>=3 || targets[slot]!=(uint32_t *)stream.buffers[idx].data || stream.buffers[idx].state!=CAMERA_BUFFER_DMA){callback_error=1;return SYS_ERR_IO;}
 if(replace_result!=SYS_OK)return replace_result;
 targets[slot]=buffer;return SYS_OK;
}
static int Begin(uint32_t format)
{
 memset(&stream,0,sizeof(stream));config.width=160;config.height=120;config.format=(CameraPixelFormat_t)format;
 mask=pending=inject_on_disable=pattern=callback_error=0;replace_result=SYS_OK;
 EQ(CameraStream_Init(&stream,&config),SYS_OK);
 targets[0]=(uint32_t *)stream.buffers[0].data;targets[1]=(uint32_t *)stream.buffers[1].data;
 return 0;
}
static int Valid(void)
{
 uint32_t i,dma=0,other=0;
 EQ(stream.dma_m0_index<3,1);EQ(stream.dma_m1_index<3,1);EQ(stream.dma_m0_index!=stream.dma_m1_index,1);
 EQ(targets[0],stream.buffers[stream.dma_m0_index].data);EQ(targets[1],stream.buffers[stream.dma_m1_index].data);
 for(i=0;i<3;i++){
  EQ((uint32_t)stream.buffers[i].data,0xD0000400U+i*38400U);EQ(stream.buffers[i].capacity,38400);
  if(i==stream.dma_m0_index || i==stream.dma_m1_index){EQ(stream.buffers[i].state,CAMERA_BUFFER_DMA);dma++;}
  else {other++;EQ(stream.buffers[i].state==CAMERA_BUFFER_FREE || stream.buffers[i].state==CAMERA_BUFFER_READY || stream.buffers[i].state==CAMERA_BUFFER_IN_USE,1);
   if(stream.buffers[i].state==CAMERA_BUFFER_FREE)EQ(stream.consumer_index,UINT8_MAX);
   else EQ(stream.consumer_index,i);
  }
 }
 EQ(dma,2);EQ(other,1);EQ(callback_error,0);return 0;
}
static int Complete(uint32_t slot)
{
 uint32_t i;uint8_t *bytes=(uint8_t *)targets[slot];
 pattern++;for(i=0;i<stream.frame_bytes;i++)bytes[i]=(uint8_t)pattern;
 g_result.operations++;
 return CameraStream_Rotate(&stream,(BspDcmiTarget_t)slot,Replace);
}
static void Dispatch(void)
{
 uint32_t request;
 if(mask || !pending)return;
 request=pending;pending=0;g_result.deferred++;
 if(Complete(request-1)!=SYS_OK && !g_result.fail_line)g_result.fail_line=__LINE__;
}

//独立参考模型：只描述缓冲区所有权，不调用正式实现
static uint32_t model_state[3],model_sequence[3],m0,m1,consumer,sequence,drops,rng;
static uint32_t Random(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static int ModelRun(void)
{
 uint32_t i,op,slot,old,replacement,j,calls;SysResult_t expected;
 CameraFrame_t frame;
 m0=0;m1=1;consumer=255;sequence=drops=0;rng=0x13579BDF;
 model_state[0]=model_state[1]=CAMERA_BUFFER_DMA;model_state[2]=CAMERA_BUFFER_FREE;
 memset(model_sequence,0,sizeof(model_sequence));
 for(i=0;i<3000;i++){
  op=Random()%5;calls=g_result.replaces;
  if(op<2 || op==4){
   slot=op==4?(Random()&1):op;old=slot==0?m0:m1;replacement=3-m0-m1;
   replace_result=op==4?SYS_ERR_IO:SYS_OK;
   if(model_state[replacement]==CAMERA_BUFFER_IN_USE){sequence++;drops++;expected=SYS_OK;EQ(g_result.replaces,calls);}
   else if(replace_result!=SYS_OK)expected=SYS_ERR_IO;
   else {
    expected=SYS_OK;if(model_state[replacement]==CAMERA_BUFFER_READY)drops++;
    model_state[replacement]=CAMERA_BUFFER_DMA;model_state[old]=CAMERA_BUFFER_READY;
    model_sequence[old]=++sequence;consumer=old;if(slot==0)m0=replacement;else m1=replacement;
   }
   EQ(Complete(slot),expected);replace_result=SYS_OK;
  }else if(op==2){
   expected=(consumer<3 && model_state[consumer]==CAMERA_BUFFER_READY);
   EQ(CameraStream_GetFrame(&stream,&config,&frame),expected);
   if(expected){model_state[consumer]=CAMERA_BUFFER_IN_USE;EQ(frame.data,stream.buffers[consumer].data);EQ(frame.size_bytes,stream.frame_bytes);}
  }else{
   expected=(consumer<3 && model_state[consumer]==CAMERA_BUFFER_IN_USE)?SYS_OK:SYS_ERR_INVALID_STATE;
   EQ(CameraStream_ReleaseFrame(&stream),expected);
   if(expected==SYS_OK){model_state[consumer]=CAMERA_BUFFER_FREE;consumer=255;}
  }
  EQ(stream.dma_m0_index,m0);EQ(stream.dma_m1_index,m1);EQ(stream.consumer_index,consumer);
  EQ(stream.next_sequence,sequence);EQ(stream.dropped_frames,drops);
  for(j=0;j<3;j++){EQ(stream.buffers[j].state,model_state[j]);EQ(stream.buffers[j].sequence,model_sequence[j]);}
  EQ(Valid(),0);EQ(mask,0);
 }
 return 0;
}

int Stream_TestRun(uint32_t id)
{
 uint32_t kind=id/3,format=id%3,i,j,held,n;CameraFrame_t frame={0},old_frame;CameraStream_t before;
 SysResult_t codes[3]={SYS_ERR_IO,SYS_ERR_BUSY,SYS_ERR_TIMEOUT};CameraConfig_t bad;
 g_result.case_id=id;EQ(Begin(format),0);
 if(kind==0){
  EQ(stream.frame_bytes,format==0?19200:38400);EQ(stream.next_sequence,0);EQ(stream.dropped_frames,0);
  EQ(((uint32_t)stream.buffers[2].data+38400)<=0xD0020000,1);
  for(i=0;i<3;i++)EQ(((uint32_t)stream.buffers[i].data%32),0);
  EQ(Valid(),0);return 0;
 }
 if(kind==1){EQ(CameraStream_FindReplaceable(&stream),2);EQ(CameraStream_GetFrame(&stream,&config,&frame),false);EQ(CameraStream_ReleaseFrame(&stream),SYS_ERR_INVALID_STATE);return Valid();}
 if(kind==2 || kind==3){
  EQ(Complete(kind==2?0:1),SYS_OK);EQ(Valid(),0);EQ(stream.consumer_index,kind==2?0:1);
  EQ(CameraStream_GetFrame(&stream,&config,&frame),true);EQ(frame.width,160);EQ(frame.height,120);EQ(frame.format,format);EQ(frame.size_bytes,stream.frame_bytes);
  EQ(CameraStream_GetFrame(&stream,&config,&frame),false);EQ(CameraStream_ReleaseFrame(&stream),SYS_OK);
  EQ(CameraStream_ReleaseFrame(&stream),SYS_ERR_INVALID_STATE);return Valid();
 }
 if(kind==4){
  for(i=0;i<30;i++){EQ(Complete(i%2),SYS_OK);EQ(Valid(),0);}
  EQ(stream.next_sequence,30);EQ(stream.dropped_frames,29);
  EQ(CameraStream_GetFrame(&stream,&config,&frame),true);
  for(i=0;i<stream.frame_bytes;i++)EQ(frame.data[i],30);
  EQ(CameraStream_ReleaseFrame(&stream),SYS_OK);return Valid();
 }
 if(kind==5){
  EQ(Complete(0),SYS_OK);EQ(CameraStream_GetFrame(&stream,&config,&frame),true);held=stream.consumer_index;n=g_result.replaces;
  for(i=0;i<1000;i++){EQ(Complete(i%2),SYS_OK);EQ(stream.consumer_index,held);EQ(Valid(),0);}
  EQ(g_result.replaces,n);EQ(stream.dropped_frames,1000);EQ(stream.next_sequence,1001);
  for(i=0;i<stream.frame_bytes;i++)EQ(frame.data[i],1);
  EQ(CameraStream_ReleaseFrame(&stream),SYS_OK);EQ(Complete(0),SYS_OK);EQ(Valid(),0);
  EQ(CameraStream_GetFrame(&stream,&config,&frame),true);EQ(stream.buffers[stream.consumer_index].sequence,1002);return 0;
 }
 if(kind==6 || kind==7){
  for(i=0;i<3;i++){
   EQ(Begin(format),0);if(kind==7)EQ(Complete(0),SYS_OK);before=stream;
   replace_result=codes[i];EQ(Complete(1),codes[i]);SAME(stream,before);EQ(Valid(),0);
   if(kind==7)for(j=0;j<stream.frame_bytes;j++)EQ(stream.buffers[stream.consumer_index].data[j],1);
   replace_result=SYS_OK;EQ(Complete(1),SYS_OK);EQ(Valid(),0);
  }return 0;
 }
 if(kind==8){
  before=stream;EQ(CameraStream_Init(NULL,&config),SYS_ERR_INVALID_ARG);EQ(CameraStream_Init(&stream,NULL),SYS_ERR_INVALID_ARG);SAME(stream,before);
  bad=config;bad.width=159;EQ(CameraStream_Init(&stream,&bad),SYS_ERR_INVALID_ARG);SAME(stream,before);
  bad=config;bad.format=(CameraPixelFormat_t)99;EQ(CameraStream_Init(&stream,&bad),SYS_ERR_INVALID_ARG);SAME(stream,before);return 0;
 }
 if(kind==9){
  before=stream;EQ(CameraStream_Rotate(NULL,BSP_DCMI_TARGET_M0,Replace),SYS_ERR_INVALID_ARG);
  EQ(CameraStream_Rotate(&stream,BSP_DCMI_TARGET_M0,NULL),SYS_ERR_INVALID_ARG);
  EQ(CameraStream_Rotate(&stream,(BspDcmiTarget_t)2,Replace),SYS_ERR_INVALID_ARG);SAME(stream,before);EQ(g_result.replaces,0);return 0;
 }
 if(kind==10 || kind==11 || kind==12){
  if(kind==10)stream.dma_m0_index=3;
  if(kind==11)stream.dma_m1_index=stream.dma_m0_index;
  if(kind==12)stream.buffers[stream.dma_m0_index].state=CAMERA_BUFFER_READY;
  before=stream;EQ(CameraStream_Rotate(&stream,BSP_DCMI_TARGET_M0,Replace),SYS_ERR_INVALID_STATE);SAME(stream,before);EQ(g_result.replaces,0);return 0;
 }
 if(kind==13 || kind==14 || kind==15 || kind==16){
  if(kind==13)stream.consumer_index=3;
  if(kind==14)stream.consumer_index=stream.dma_m0_index;
  if(kind==15){stream.consumer_index=2;stream.buffers[2].state=CAMERA_BUFFER_FREE;}
  if(kind==16){stream.consumer_index=255;stream.buffers[2].state=CAMERA_BUFFER_READY;}
  before=stream;EQ(CameraStream_Rotate(&stream,BSP_DCMI_TARGET_M0,Replace),SYS_ERR_INVALID_STATE);SAME(stream,before);EQ(g_result.replaces,0);return 0;
 }
 if(kind==17){
  for(i=0;i<4;i++){
   EQ(Begin(format),0);if(i==0)stream.buffers[2].data=NULL;
   if(i==1)stream.frame_bytes=0;if(i==2)stream.buffers[2].capacity=1;if(i==3)stream.frame_bytes=3;
   before=stream;EQ(CameraStream_Rotate(&stream,BSP_DCMI_TARGET_M0,Replace),SYS_ERR_INVALID_STATE);SAME(stream,before);EQ(g_result.replaces,0);
  }return 0;
 }
 if(kind==18){
  old_frame=frame;EQ(CameraStream_GetFrame(NULL,&config,&frame),false);EQ(CameraStream_GetFrame(&stream,NULL,&frame),false);EQ(CameraStream_GetFrame(&stream,&config,NULL),false);SAME(frame,old_frame);
  EQ(CameraStream_ReleaseFrame(NULL),SYS_ERR_INVALID_ARG);return 0;
 }
 if(kind==19 || kind==20){
  EQ(Complete(0),SYS_OK);mask=kind==19?0:1;EQ(CameraStream_GetFrame(&stream,&config,&frame),true);EQ(mask,kind==19?0:1);
  EQ(CameraStream_GetFrame(&stream,&config,&frame),false);EQ(mask,kind==19?0:1);
  EQ(CameraStream_ReleaseFrame(&stream),SYS_OK);EQ(mask,kind==19?0:1);
  EQ(CameraStream_ReleaseFrame(&stream),SYS_ERR_INVALID_STATE);EQ(mask,kind==19?0:1);return 0;
 }
 if(kind==21){
  EQ(Complete(0),SYS_OK);inject_on_disable=2;
  EQ(CameraStream_GetFrame(&stream,&config,&frame),true);EQ(g_result.deferred,1);EQ(stream.buffers[stream.consumer_index].state,CAMERA_BUFFER_IN_USE);
  EQ(stream.dropped_frames,1);for(i=0;i<stream.frame_bytes;i++)EQ(frame.data[i],1);return Valid();
 }
 if(kind==22){
  EQ(Complete(0),SYS_OK);EQ(CameraStream_GetFrame(&stream,&config,&frame),true);inject_on_disable=2;
  EQ(CameraStream_ReleaseFrame(&stream),SYS_OK);EQ(g_result.deferred,1);EQ(stream.buffers[stream.consumer_index].state,CAMERA_BUFFER_READY);return Valid();
 }
 if(kind==23){
  EQ(Complete(0),SYS_OK);mask=1;inject_on_disable=2;
  EQ(CameraStream_GetFrame(&stream,&config,&frame),true);EQ(mask,1);EQ(pending,2);EQ(g_result.deferred,0);
  __set_PRIMASK(0);EQ(g_result.deferred,1);EQ(stream.dropped_frames,1);return Valid();
 }
 if(kind==24)return ModelRun();
 return 1;
}

SysResult_t BSP_DCMI_StartContinuous(uint32_t *m0,uint32_t *m1,uint32_t n,BspDcmiFrameCallback_t cb,void *context){(void)m0;(void)m1;(void)n;(void)cb;(void)context;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t target,uint32_t *buffer){(void)target;(void)buffer;return SYS_ERR_NOT_SUPPORTED;}
