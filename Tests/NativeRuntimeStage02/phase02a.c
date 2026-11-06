#include "../../CM7/Media/Src/camera_capture.c"
#include <string.h>
static uint32_t irq_mask,notifications,last_events;
static SysResult_t replace_result;
static void *seen_context;
void *memset(void *d,int v,size_t n){uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
void *memcpy(void *d,const void *s,size_t n){uint8_t *p=d;const uint8_t *q=s;while(n--)*p++=*q++;return d;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
uint32_t __get_PRIMASK(void){return irq_mask;}
void __disable_irq(void){irq_mask=1;}
void __set_PRIMASK(uint32_t x){irq_mask=x;}
uint32_t Platform_TimeGetMs(void){return 0;}
SysResult_t BSP_DCMI_StartSnapshot(uint32_t *b,uint32_t n){(void)b;(void)n;return SYS_OK;}
SysResult_t BSP_DCMI_StartContinuous(uint32_t *a,uint32_t *b,uint32_t n,BspDcmiFrameCallback_t c,void *p){(void)a;(void)b;(void)n;(void)c;(void)p;return SYS_OK;}
SysResult_t BSP_DCMI_Stop(void){return SYS_OK;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *e){(void)e;return false;}
SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t t,uint32_t *p){(void)t;(void)p;return replace_result;}
static void Notify(uint32_t e,void *p){notifications++;last_events=e;seen_context=p;}
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)
int Stage02_TestRun(uint32_t id)
{
    CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_YUV422};CameraCaptureStats_t stats;CameraFrame_t frame;
    if(id==0){CHECK(CameraCapture_SetEventCallback(NULL,NULL)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==1){CHECK(CameraCapture_GetStats(NULL)==SYS_ERR_INVALID_ARG);CHECK(CameraCapture_GetStats(&stats)==SYS_ERR_INVALID_STATE);return 0;}
    CHECK(CameraCapture_SetEventCallback(Notify,(void *)0x1234)==SYS_OK);
    if(id==2){CHECK(CameraCapture_SetEventCallback(Notify,NULL)==SYS_ERR_INVALID_STATE);return 0;}
    CHECK(CameraCapture_Init(&cfg)==SYS_OK);CHECK(CameraCapture_StartStream()==SYS_OK);
    if(id==3){CHECK(notifications==0);CHECK(CameraCapture_SetEventCallback(Notify,NULL)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==4){CHECK(CameraStream_OnFrameComplete(BSP_DCMI_TARGET_M0,NULL)==SYS_ERR_INVALID_ARG);CHECK(notifications==0);return 0;}
    replace_result=id==5?SYS_ERR_IO:SYS_OK;
    CHECK(CameraStream_OnFrameComplete(BSP_DCMI_TARGET_M0,&s_capture.stream)==replace_result);
    CHECK(notifications==1 && seen_context==(void *)0x1234);
    if(id==5){CHECK(last_events==CAMERA_CAPTURE_EVENT_ERROR);CHECK(s_capture.stream.next_sequence==0);return 0;}
    CHECK(last_events==CAMERA_CAPTURE_EVENT_PROGRESS);CHECK(CameraCapture_GetStats(&stats)==SYS_OK);CHECK(stats.completed_frames==1);
    if(id==6)return 0;
    CHECK(CameraCapture_StreamGetFrame(&frame));CHECK(frame.size_bytes==38400);
    if(id==7){CHECK(CameraCapture_Stop()==SYS_ERR_BUSY);CHECK(CameraCapture_StreamReleaseFrame()==SYS_OK);CHECK(CameraCapture_Stop()==SYS_OK);return 0;}
    CHECK(CameraStream_OnFrameComplete(BSP_DCMI_TARGET_M1,&s_capture.stream)==SYS_OK);
    CHECK(CameraCapture_GetStats(&stats)==SYS_OK);CHECK(stats.completed_frames==2 && stats.dropped_frames==0);CHECK(notifications==2);
    if(id==8)return 0;
    irq_mask=1;CHECK(CameraCapture_GetStats(&stats)==SYS_OK);CHECK(irq_mask==1);return 0;
}

//旧格式专项不产生JPEG事件，新增接口显式返回不支持
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *p,uint32_t n){(void)p;(void)n;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *n){(void)n;return SYS_ERR_NOT_SUPPORTED;}
