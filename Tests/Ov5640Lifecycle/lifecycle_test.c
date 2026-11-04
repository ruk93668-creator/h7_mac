//编译真实Device、OV5640组件、Application和Media，仅模拟板级寄存器与DCMI
#include "../../CM7/Device/Src/device.c"
#include "../../CM7/Device/Src/ov5640_device.c"
#include "../../CM7/Media/Src/camera_capture.c"
#include "../../CM7/Application/app_camera.c"
#include "../../CM7/Application/video_config.c"
#include <string.h>

static uint8_t regs[65536];
static uint32_t tick, mask, writes, sensor_starts, sensor_stops, dcmi_starts, dcmi_stops;
static uint32_t step, dcmi_stop_step, sensor_stop_step, events;
volatile uint32_t g_unsafe_sensor_stops, g_dma_before_sensor;
static int32_t fail_reg=-1, fail_value=-1;
static SysResult_t media_start_result, media_stop_result, board_result;
static BspDcmiFrameCallback_t callback;
static void *cb_context;
static bool reenter;
static SysResult_t nested_start, nested_stop;
volatile uint32_t g_checks;
#define CHECK(x) do {g_checks++;if(!(x))return __LINE__;} while(0)

void *memset(void *d,int v,size_t n){volatile uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
void *memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;return d;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
uint32_t Platform_TimeGetMs(void){return tick++;}
uint32_t __get_PRIMASK(void){return mask;}
void __disable_irq(void){mask=1;}
void __set_PRIMASK(uint32_t value){mask=value;}
SysResult_t BSP_Camera_Init(void){return board_result;}
SysResult_t BSP_Camera_ReadReg(uint16_t reg,uint8_t *value){
 if((int32_t)reg==fail_reg && fail_value==-2)return SYS_ERR_IO;
 *value=regs[reg];return SYS_OK;
}
SysResult_t BSP_Camera_WriteReg(uint16_t reg,uint8_t value){
 writes++;step++;
 if((int32_t)reg==fail_reg && (fail_value<0 || fail_value==value))return SYS_ERR_IO;
 regs[reg]=value;
 if(reg==0x3008 && value==2){sensor_starts++;}
 if(reg==0x3008 && value==0x42){sensor_stops++;sensor_stop_step=step;
  if(s_camera_initialized && !s_configuring && !CameraCapture_IsStopped())g_unsafe_sensor_stops++;
 }
 if(reenter && reg==0x3008){
  reenter=false;nested_start=Device_Start(&s_ov5640.base);nested_stop=Device_Stop(&s_ov5640.base);
 }
 return SYS_OK;
}
SysResult_t BSP_DCMI_StartSnapshot(uint32_t *p,uint32_t n){(void)p;(void)n;if(regs[0x3008]!=2)g_dma_before_sensor++;dcmi_starts++;return media_start_result;}
SysResult_t BSP_DCMI_StartContinuous(uint32_t *a,uint32_t *b,uint32_t n,BspDcmiFrameCallback_t cb,void *ctx){
 (void)a;(void)b;(void)n;if(regs[0x3008]!=2)g_dma_before_sensor++;dcmi_starts++;callback=cb;cb_context=ctx;return media_start_result;
}
SysResult_t BSP_DCMI_Stop(void){dcmi_stops++;dcmi_stop_step=++step;return media_stop_result;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *e){
 if(!events)return false;e->flags=events;e->result=(events & BSP_DCMI_EVENT_ERROR)?SYS_ERR_IO:SYS_OK;events=0;return true;
}
SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t t,uint32_t *p){(void)t;(void)p;return SYS_OK;}
static int Init(void){
 regs[0x300a]=0x56;regs[0x300b]=0x40;
 CHECK(Device_Register(&s_ov5640.base)==SYS_OK);
 CHECK(Device_Init(&s_ov5640.base)==SYS_OK);
 CHECK(s_ov5640.base.state==DEVICE_STATE_READY);return 0;
}
static int Configure(CameraPixelFormat_t format){
 CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_YUV422};cfg.format=format;
 CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_OK);
 CHECK(s_ov5640.base.state==DEVICE_STATE_READY);
 CHECK(regs[0x3008]==0x42 && sensor_starts==0 && s_sensor_configured);
 return 0;
}
static int HoldFrame(CameraFrame_t *frame){
 CHECK(callback(BSP_DCMI_TARGET_M0,cb_context)==SYS_OK);
 CHECK(CameraCapture_StreamGetFrame(frame));return 0;
}
int Lifecycle_TestRun(uint32_t id){
 CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_YUV422};CameraFrame_t frame;
 uint32_t before;SysResult_t r;
 if(id==26){CHECK(App_CameraStop()==SYS_ERR_INVALID_STATE);return 0;}
 CHECK(Init()==0);
 if(id<3){CHECK(Configure((CameraPixelFormat_t)id)==0);return 0;}
 if(id==4){CHECK(Device_Start(&s_ov5640.base)==SYS_ERR_INVALID_STATE);CHECK(!sensor_starts && s_ov5640.base.state==DEVICE_STATE_ERROR);return 0;}
 if(id==6){cfg.width=0;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_INVALID_ARG);CHECK(!writes && s_ov5640.base.state==DEVICE_STATE_READY);cfg.width=160;s_ov5640.base.state=DEVICE_STATE_BUSY;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_BUSY);CHECK(!writes && s_ov5640.base.state==DEVICE_STATE_BUSY);return 0;}
 if(id==7){regs[0x300b]=0;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_PROTOCOL);CHECK(!writes && !s_sensor_configured && s_ov5640.base.state==DEVICE_STATE_READY);return 0;}
 if(id==8){fail_reg=0x380c;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_IO);CHECK(!s_sensor_configured && !sensor_starts && !s_configuring && s_ov5640.base.state==DEVICE_STATE_READY);fail_reg=-1;CHECK(Configure(CAMERA_PIXEL_FORMAT_YUV422)==0);return 0;}
 if(id==27){fail_reg=0x300a;fail_value=-2;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_IO);CHECK(!writes && s_ov5640.base.state==DEVICE_STATE_READY);return 0;}
 if(id==28){fail_reg=0x3008;fail_value=0x42;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_IO);CHECK(!sensor_starts && !s_sensor_configured && !s_configuring && s_ov5640.base.state==DEVICE_STATE_READY);return 0;}
 if(id==23){board_result=SYS_ERR_IO;CHECK(App_CameraInitStream()==SYS_ERR_IO);CHECK(!sensor_starts && !dcmi_starts && s_ov5640.base.state==DEVICE_STATE_READY);return 0;}
 if(id==24){CHECK(App_CameraPrepare()==SYS_OK);fail_reg=0x3008;fail_value=2;CHECK(App_CameraInitStream()==SYS_ERR_IO);CHECK(!dcmi_starts && s_ov5640.base.state==DEVICE_STATE_ERROR);return 0;}
 if(id==19 || id==20){media_start_result=SYS_ERR_IO;if(id==20)media_stop_result=SYS_ERR_TIMEOUT;r=App_CameraInitStream();CHECK(r==(id==19?SYS_ERR_IO:SYS_ERR_TIMEOUT));CHECK(sensor_starts==1 && dcmi_stops==1);if(id==19){CHECK(s_ov5640.base.state==DEVICE_STATE_READY && CameraCapture_IsStopped());CHECK(sensor_stop_step>dcmi_stop_step);}else{CHECK(s_ov5640.base.state==DEVICE_STATE_RUNNING && !CameraCapture_IsStopped());}return 0;}
 if(id==3 || id==5 || id==9 || id==10 || id==31){
  CHECK(Configure(CAMERA_PIXEL_FORMAT_YUV422)==0);
  if(id==9){fail_reg=0x3008;fail_value=2;CHECK(Device_Start(&s_ov5640.base)==SYS_ERR_IO);CHECK(s_ov5640.base.state==DEVICE_STATE_ERROR && !sensor_starts);return 0;}
  if(id==31)reenter=true;
  CHECK(Device_Start(&s_ov5640.base)==SYS_OK);CHECK(sensor_starts==1 && s_ov5640.base.state==DEVICE_STATE_RUNNING);
  if(id==31){CHECK(nested_start==SYS_ERR_BUSY && nested_stop==SYS_ERR_BUSY);reenter=true;}
  if(id==5){before=writes;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_INVALID_STATE);CHECK(before==writes && s_ov5640.base.state==DEVICE_STATE_RUNNING);return 0;}
  if(id==10){fail_reg=0x3008;fail_value=0x42;CHECK(Device_Stop(&s_ov5640.base)==SYS_ERR_IO);CHECK(s_ov5640.base.state==DEVICE_STATE_ERROR);return 0;}
  CHECK(Device_Stop(&s_ov5640.base)==SYS_OK);CHECK(s_ov5640.base.state==DEVICE_STATE_READY && regs[0x3008]==0x42);
  if(id==31)CHECK(nested_start==SYS_ERR_BUSY && nested_stop==SYS_ERR_BUSY);
  CHECK(Device_Stop(&s_ov5640.base)==SYS_ERR_INVALID_STATE);return 0;
 }
 CHECK((id==12 || id==15 || id==30 ? App_CameraInit():App_CameraInitStream())==SYS_OK);
 CHECK(s_ov5640.base.state==DEVICE_STATE_RUNNING && sensor_starts==1 && dcmi_starts==1 && !CameraCapture_IsStopped());
 if(id==13 || id==14){CHECK((id==13?App_CameraInitStream():App_CameraInit())==SYS_ERR_BUSY);CHECK(sensor_starts==1 && s_ov5640.base.state==DEVICE_STATE_RUNNING);return 0;}
 if(id==15 || id==30){events=BSP_DCMI_EVENT_FRAME;CameraCapture_Process();CHECK(CameraCapture_GetFrame(&frame));CHECK(!CameraCapture_IsStopped());CHECK(App_CameraStop()==SYS_ERR_BUSY);CHECK(s_ov5640.base.state==DEVICE_STATE_RUNNING);CHECK(CameraCapture_ReleaseFrame()==SYS_OK);if(id==30){App_CameraRun();CHECK(s_ov5640.base.state==DEVICE_STATE_READY);return 0;}}
 if(id==16 || id==17 || id==22){CHECK(HoldFrame(&frame)==0);before=dcmi_stops;
  if(id==17 || id==22){events=BSP_DCMI_EVENT_ERROR;CameraCapture_Process();CHECK(s_capture.state==CAMERA_CAPTURE_DRAINING && !CameraCapture_IsStopped());}
  CHECK(App_CameraStop()==SYS_ERR_BUSY);CHECK(s_ov5640.base.state==DEVICE_STATE_RUNNING);
  if(id==16)CHECK(dcmi_stops==before);
  else CHECK(dcmi_stops==before+1);
  CHECK(CameraCapture_StreamReleaseFrame()==SYS_OK);
  if(id==22){App_CameraRun();CHECK(s_ov5640.base.state==DEVICE_STATE_READY);return 0;}
 }
 if(id==18){media_stop_result=SYS_ERR_TIMEOUT;CHECK(App_CameraStop()==SYS_ERR_TIMEOUT);CHECK(s_ov5640.base.state==DEVICE_STATE_RUNNING && !CameraCapture_IsStopped());media_stop_result=SYS_OK;}
 if(id==21){events=BSP_DCMI_EVENT_ERROR;App_CameraRun();CHECK(CameraCapture_IsStopped() && s_ov5640.base.state==DEVICE_STATE_READY);CHECK(sensor_stop_step>dcmi_stop_step);return 0;}
 if(id==25){fail_reg=0x3008;fail_value=0x42;CHECK(App_CameraStop()==SYS_ERR_IO);CHECK(CameraCapture_IsStopped() && s_ov5640.base.state==DEVICE_STATE_ERROR);CHECK(App_CameraInitStream()==SYS_ERR_INVALID_STATE);return 0;}
 CHECK(App_CameraStop()==SYS_OK);CHECK(CameraCapture_IsStopped() && s_ov5640.base.state==DEVICE_STATE_READY);CHECK(sensor_stop_step>dcmi_stop_step);CHECK(App_CameraStop()==SYS_OK);
 if(id==29){before=writes;CHECK(App_CameraInit()==SYS_OK);CHECK(sensor_starts==2 && writes==before+1);CHECK(s_ov5640.base.state==DEVICE_STATE_RUNNING);}
 return 0;
}
