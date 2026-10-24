//直接编译当前正式Media，BSP与时间使用可控桩
#include "../../CM7/Media/Src/camera_capture.c"
#include <string.h>
typedef struct {uint32_t fail_line,checks,operations,replaces,deferred,case_id;} TestResult;
volatile TestResult g_result;
static uint32_t tick,mask,starts,snapshots,stops,flags,words;
static SysResult_t start_result=SYS_OK,stop_result=SYS_OK,event_result=SYS_OK,replace_result=SYS_OK;
static void *callback_context;
static BspDcmiFrameCallback_t frame_callback;
static CameraConfig_t config;
void *memcpy(void *d,const void *s,size_t n){uint8_t *p=d;const uint8_t *q=s;while(n--)*p++=*q++;return d;}
void *memset(void *d,int v,size_t n){uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
int memcmp(const void *a,const void *b,size_t n){const uint8_t *p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;p++;q++;}return 0;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}

uint32_t Platform_TimeGetMs(void){return tick;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t value){mask=value;}
SysResult_t BSP_DCMI_StartSnapshot(uint32_t *p,uint32_t n){(void)p;snapshots++;words=n;return start_result;}
SysResult_t BSP_DCMI_StartContinuous(uint32_t *m0,uint32_t *m1,uint32_t n,BspDcmiFrameCallback_t cb,void *context)
{(void)m0;(void)m1;starts++;words=n;frame_callback=cb;callback_context=context;return start_result;}
SysResult_t BSP_DCMI_Stop(void){stops++;return stop_result;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *e){if(!flags)return false;e->flags=flags;e->result=event_result;flags=0;return true;}
SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t target,uint32_t *buffer){(void)target;(void)buffer;g_result.replaces++;return replace_result;}
static int Eq(uint32_t line,uint32_t a,uint32_t b){g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Eq(__LINE__,(uint32_t)(a),(uint32_t)(b)))return 1;}while(0)
static int NoneIdle(void){EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);EQ(s_capture.state,CAMERA_CAPTURE_IDLE);return 0;}
static int Ready(void){EQ(CameraCapture_Start(),SYS_OK);flags=BSP_DCMI_EVENT_FRAME;CameraCapture_Process();EQ(s_capture.state,CAMERA_CAPTURE_READY);return 0;}
int Stream_TestRun(uint32_t id)
{
 uint32_t kind=id/3,fmt=id%3,bytes=fmt==0?19200:38400;
 CameraFrame_t frame={0};CameraStream_t foreign={0},saved;
 g_result.case_id=id;config.width=160;config.height=120;config.format=(CameraPixelFormat_t)fmt;
 if(kind==16){CameraConfig_t bad=config;bad.width=159;EQ(CameraCapture_Init(&bad),SYS_ERR_INVALID_ARG);EQ(s_capture.initialized,0);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);return 0;}
 EQ(CameraCapture_Init(&config),SYS_OK);EQ(NoneIdle(),0);
 EQ(s_capture.snapshot.frame_bytes,bytes);EQ(s_capture.snapshot.valid_bytes,0);EQ(s_capture.snapshot.framebuffer,0xD0000400);
 if(kind==0){EQ(s_capture.snapshot.frame_capacity,63*1024);EQ(s_capture.snapshot.timeout_ms,100);return 0;}
 if(kind==1){EQ(Ready(),0);EQ(CameraCapture_ReleaseFrame(),SYS_OK);EQ(NoneIdle(),0);EQ(CameraCapture_StartStream(),SYS_OK);EQ(s_capture.stream.frame_bytes,bytes);return 0;}
 if(kind==3){EQ(CameraCapture_Start(),SYS_OK);EQ(CameraCapture_Stop(),SYS_OK);EQ(NoneIdle(),0);EQ(CameraCapture_StartStream(),SYS_OK);return 0;}
 if(kind==11){EQ(Ready(),0);EQ(CameraCapture_GetFrame(&frame),true);EQ(frame.size_bytes,bytes);EQ(frame.data,s_capture.snapshot.framebuffer);EQ(frame.format,fmt);EQ(CameraCapture_ReleaseFrame(),SYS_OK);EQ(NoneIdle(),0);EQ(CameraCapture_GetFrame(&frame),false);EQ(CameraCapture_ReleaseFrame(),SYS_ERR_INVALID_STATE);EQ(CameraCapture_Start(),SYS_OK);return 0;}
 if(kind==12){start_result=SYS_ERR_IO;EQ(CameraCapture_Start(),SYS_ERR_IO);stop_result=SYS_ERR_TIMEOUT;EQ(CameraCapture_Stop(),SYS_ERR_TIMEOUT);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_SNAPSHOT);EQ(s_capture.state,CAMERA_CAPTURE_ERROR);EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);stop_result=SYS_OK;EQ(CameraCapture_Stop(),SYS_OK);EQ(NoneIdle(),0);return 0;}
 if(kind==13){EQ(Ready(),0);EQ(CameraCapture_StartStream(),SYS_ERR_BUSY);EQ(CameraCapture_Stop(),SYS_ERR_BUSY);EQ(starts,0);return 0;}
 if(kind==15){EQ(CameraCapture_Stop(),SYS_OK);EQ(stops,0);EQ(NoneIdle(),0);return 0;}
 if(kind==17){s_capture.config.width=159;EQ(CameraCapture_StartStream(),SYS_ERR_INVALID_ARG);EQ(NoneIdle(),0);EQ(starts,0);return 0;}
 if(kind==21 || kind==22){tick=0xFFFFFFF0;EQ(CameraCapture_Start(),SYS_OK);tick=kind==21?0x53:0x54;CameraCapture_Process();EQ(s_capture.state,kind==21?CAMERA_CAPTURE_CAPTURING:CAMERA_CAPTURE_IDLE);EQ(stops,kind==21?0:1);if(kind==22){EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);EQ(CameraCapture_GetLastResult(),SYS_ERR_TIMEOUT);}return 0;}
 if(kind==4 || kind==5 || kind==18 || kind==19){
  start_result=kind==18?SYS_ERR_BUSY:kind==19?SYS_ERR_TIMEOUT:SYS_ERR_IO;
  EQ(CameraCapture_StartStream(),start_result);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_STREAM);EQ(s_capture.state,CAMERA_CAPTURE_ERROR);
  EQ(CameraCapture_Start(),SYS_ERR_INVALID_STATE);EQ(CameraCapture_StartStream(),SYS_ERR_INVALID_STATE);
  if(kind==5){stop_result=SYS_ERR_TIMEOUT;EQ(CameraCapture_Stop(),SYS_ERR_TIMEOUT);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_STREAM);EQ(s_capture.state,CAMERA_CAPTURE_ERROR);EQ(CameraCapture_GetLastResult(),start_result);stop_result=SYS_OK;}
  EQ(CameraCapture_Stop(),SYS_OK);EQ(NoneIdle(),0);EQ(CameraCapture_GetLastResult(),start_result);return 0;
 }
 EQ(CameraCapture_StartStream(),SYS_OK);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_STREAM);EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);
 EQ(callback_context,&s_capture.stream);EQ(words,bytes/4);EQ(frame_callback==CameraStream_OnFrameComplete,1);
 if(kind==2){EQ(CameraCapture_Stop(),SYS_OK);EQ(NoneIdle(),0);EQ(CameraCapture_Start(),SYS_OK);return 0;}
 if(kind==6){tick=100000;CameraCapture_Process();EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);EQ(stops,0);return 0;}
 if(kind==7){flags=BSP_DCMI_EVENT_FRAME;CameraCapture_Process();EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);EQ(s_capture.snapshot.valid_bytes,0);EQ(CameraCapture_GetFrame(&frame),false);return 0;}
 if(kind==8 || kind==9){flags=BSP_DCMI_EVENT_ERROR;event_result=SYS_ERR_IO;if(kind==9)stop_result=SYS_ERR_TIMEOUT;CameraCapture_Process();EQ(stops,1);EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);if(kind==9){EQ(s_capture.mode,CAMERA_CAPTURE_MODE_STREAM);EQ(s_capture.state,CAMERA_CAPTURE_ERROR);stop_result=SYS_OK;CameraCapture_Process();EQ(stops,2);}EQ(NoneIdle(),0);return 0;}
 if(kind==10){EQ(frame_callback(BSP_DCMI_TARGET_M0,&foreign),SYS_ERR_INVALID_ARG);EQ(g_result.replaces,0);EQ(frame_callback(BSP_DCMI_TARGET_M0,callback_context),SYS_OK);EQ(g_result.replaces,1);EQ(s_capture.stream.next_sequence,1);EQ(s_capture.stream.consumer_index,0);return 0;}
 if(kind==14){EQ(CameraCapture_Start(),SYS_ERR_BUSY);EQ(CameraCapture_StartStream(),SYS_ERR_BUSY);EQ(snapshots,0);EQ(starts,1);return 0;}
 if(kind==20){saved=s_capture.stream;replace_result=SYS_ERR_IO;EQ(frame_callback(BSP_DCMI_TARGET_M0,callback_context),SYS_ERR_IO);EQ(memcmp(&saved,&s_capture.stream,sizeof(saved)),0);return 0;}
 if(kind==23){mask=1;EQ(CameraStream_GetFrame(&s_capture.stream,&config,&frame),false);EQ(mask,1);EQ(CameraStream_ReleaseFrame(&s_capture.stream),SYS_ERR_INVALID_STATE);EQ(mask,1);return 0;}
 EQ(0,1);return 1;
}
