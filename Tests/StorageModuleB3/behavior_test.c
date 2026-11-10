//运行基线和候选Storage、Task及真实JPEG/AVI，介质和任务环境仅由测试桩提供
#include "storage_task.h"
#include "app_storage.h"
#include "app_events.h"
#include "video_config.h"
#include "platform_sync.h"
#include <string.h>
#include B3_STORAGE_SOURCE
#include B3_TASK_SOURCE
#include "../../CM7/Service/jpeg_frame.c"
#include "../../CM7/Service/mjpeg_avi.c"
#include "jpeg_data.h"

//保存依赖调用时的诊断和业务现场，比较原有更新时点
volatile uint32_t g_trace[1024][96];
volatile uint32_t g_trace_count, g_failure;
static uint32_t tick, mask, exception, basepri, waits, bad_owner, case_id;
static uint32_t fs_position, fs_size, fs_state=1, fs_open, fail_call, fail_nth=1, fail_seen;
static uint32_t retain_count, release_count, sd_owned, last_open_name;
static uint8_t file_data[262144];
static RuntimeEventTarget_t owner=RUNTIME_TARGET_STORAGE;
static SysResult_t release_error, retain_error;
static bool short_write, corrupt_read, fake_task_fail, tamper_filename;
static BaseType_t scheduler=taskSCHEDULER_NOT_STARTED;
static VideoConfig_t config={.camera={160,120,CAMERA_PIXEL_FORMAT_JPEG},.output=VIDEO_OUTPUT_USB_UVC,.output_fps=25,.output_max_frame_bytes=70000};
static AppStorageConfig_t storage_config={25,4194304,60000,16777216,524288,4,0};
#define CHECK(x) do { if(!(x)) return __LINE__; } while(0)

//只记录数值和内容摘要，不比较两个镜像内不同的函数或缓冲地址
static uint32_t Hash(const void *p,uint32_t n){const uint8_t*q=p;uint32_t h=0;while(n--)h=h*33U+*q++;return h;}
static void Trace(uint32_t call,uint32_t a,uint32_t b)
{
 uint32_t n=g_trace_count,i; const volatile uint32_t*d=(const volatile uint32_t*)&g_storage_status;
 if(n>=1024){g_failure=9001;return;}
 g_trace[n][0]=call;g_trace[n][1]=a;g_trace[n][2]=b;g_trace[n][3]=tick;g_trace[n][4]=owner;g_trace[n][5]=mask;
 for(i=0;i<sizeof(g_storage_status)/4;i++)g_trace[n][6+i]=d[i];
 i=6+sizeof(g_storage_status)/4;
 g_trace[n][i++]=s_state;g_trace[n][i++]=s_accepting;g_trace[n][i++]=s_stop_pending;
 g_trace[n][i++]=s_session;g_trace[n][i++]=s_count;g_trace[n][i++]=s_command_count;
 g_trace[n][i++]=s_slots[0].state;g_trace[n][i++]=s_slots[0].generation;
 g_trace[n][i++]=s_mounted;g_trace[n][i++]=s_file;g_trace[n][i++]=s_avi.frames;
 g_trace[n][i++]=s_fps_tick;g_trace[n][i++]=s_fps_frames;g_trace[n][i++]=sd_owned;
 g_trace_count++;
}

//模拟相同的短临界区和原时间读取次数
uint32_t Platform_TimeGetMs(void){Trace(1,0,0);return tick;}
uint32_t Platform_SyncGetException(void){return exception;}
uint32_t Platform_SyncGetPrimask(void){return mask;}
uint32_t Platform_SyncGetBasepri(void){return basepri;}
void Platform_SyncDisableIrq(void){Trace(2,0,0);mask=1;}
void Platform_SyncRestoreIrq(uint32_t saved){Trace(3,saved,0);mask=saved;}
void Platform_SyncMemoryBarrier(void){Trace(4,0,0);}
bool App_EventsIsOwner(RuntimeEventTarget_t t){Trace(5,t,0);return owner==t;}
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t t,uint32_t e){Trace(6,t,e);return SYS_OK;}
const VideoConfig_t *VideoConfig_GetDefault(void){Trace(7,0,0);return &config;}
const AppStorageConfig_t *VideoConfig_GetStorage(void){Trace(8,0,0);return &storage_config;}

//模拟Task的创建和等待出口，等待次数由Python钩子限制
BaseType_t xTaskGetSchedulerState(void){return scheduler;}
TaskHandle_t xTaskCreateStatic(TaskFunction_t f,const char*n,const uint32_t w,void*a,UBaseType_t p,StackType_t*s,StaticTask_t*t)
{(void)f;(void)n;(void)a;(void)s;(void)t;Trace(10,w,p);return fake_task_fail?NULL:(TaskHandle_t)0x2003E000;}
SysResult_t App_EventsAttach(RuntimeEventTarget_t t,TaskHandle_t h){Trace(11,t,h!=NULL);return SYS_OK;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t){(void)t;Trace(12,0,0);return 1200;}
SysResult_t App_EventsWait(RuntimeEventTarget_t t,uint32_t ms,uint32_t*events)
{Trace(13,t,ms);waits++;if(case_id==4&&waits==3)g_storage_status.ready=0;*events=waits==2?RUNTIME_EVENT_BOOT_READY:0;tick+=501;return waits==4?SYS_ERR_IO:(waits==3?SYS_ERR_TIMEOUT:SYS_OK);}
void Platform_RtosAssert(const char*f,uint32_t n){(void)f;g_failure=n;}

//Camera是唯一真实归还者，保留失败后的SD引用
SysResult_t CameraMjpegFifo_RetainFrame(const CameraFrame_t*f,CameraMjpegLease_t*l)
{(void)f;Trace(20,sd_owned,0);if(owner!=RUNTIME_TARGET_CAMERA)bad_owner++;if(retain_error)return retain_error;if(sd_owned)return SYS_ERR_BUSY;sd_owned=1;retain_count++;l->session=1;l->id=retain_count;return SYS_OK;}
SysResult_t CameraMjpegFifo_ReleaseLease(const CameraMjpegLease_t*l)
{Trace(21,l->session,l->id);if(owner!=RUNTIME_TARGET_CAMERA)bad_owner++;if(release_error)return release_error;if(!sd_owned||l->session!=1||l->id!=retain_count)return SYS_ERR_INVALID_STATE;sd_owned=0;release_count++;return SYS_OK;}

//按依赖调用次序注入错误，不修改生产分支判断
static bool Fail(uint32_t call){if(call!=fail_call)return false;if(++fail_seen!=fail_nth)return false;fs_state=3;return true;}
static void FsOwner(void){if(owner!=RUNTIME_TARGET_STORAGE)bad_owner++;}
SysResult_t StorageFS_Mount(bool w,uint8_t p){Trace(30,w,p);FsOwner();if(Fail(30))return SYS_ERR_IO;fs_state=2;return SYS_OK;}
SysResult_t StorageFS_GetSpace(uint64_t*t,uint64_t*f){Trace(31,0,0);FsOwner();if(Fail(31))return SYS_ERR_IO;*t=64000000;*f=60000000;return SYS_OK;}
SysResult_t StorageFS_Unmount(void){Trace(32,0,0);FsOwner();if(Fail(32))return SYS_ERR_IO;if(fs_open)return SYS_ERR_BUSY;fs_state=1;return SYS_OK;}
SysResult_t StorageFS_Recover(void){Trace(33,0,0);FsOwner();if(Fail(33))return SYS_ERR_IO;fs_state=1;fs_open=0;return SYS_OK;}
SysResult_t StorageFS_GetDiagnostics(StorageFsDiagnostics_t*d){Trace(34,0,0);memset(d,0,sizeof(*d));d->state=fs_state;d->open_files=fs_open;return SYS_OK;}
SysResult_t StorageFS_Open(const char*n,uint32_t m,StorageFile_t*f)
{uint32_t h=Hash(n,strlen(n));Trace(35,m,h);FsOwner();if(Fail(35))return SYS_ERR_IO;if(m&STORAGE_FS_OPEN_CREATE_NEW){if(h==last_open_name)return SYS_ERR_ALREADY_EXISTS;last_open_name=h;fs_size=0;}else if(h!=last_open_name)return SYS_ERR_NOT_FOUND;fs_open=1;fs_position=0;*f=1;return SYS_OK;}
SysResult_t StorageFS_Write(StorageFile_t f,const void*d,uint32_t n,uint32_t*w)
{Trace(36,n,Hash(d,n));FsOwner();(void)f;*w=0;if(Fail(36))return SYS_ERR_IO;if(short_write&&n>224)n--;if(fs_position+n>sizeof(file_data))return SYS_ERR_NO_RESOURCE;memcpy(file_data+fs_position,d,n);fs_position+=n;if(fs_size<fs_position)fs_size=fs_position;*w=n;return SYS_OK;}
SysResult_t StorageFS_Read(StorageFile_t f,void*d,uint32_t n,uint32_t*r)
{Trace(37,n,0);FsOwner();(void)f;*r=0;if(Fail(37))return SYS_ERR_IO;if(n>fs_size-fs_position)n=fs_size-fs_position;memcpy(d,file_data+fs_position,n);if(corrupt_read&&n)((uint8_t*)d)[0]^=1;fs_position+=n;*r=n;return SYS_OK;}
SysResult_t StorageFS_Sync(StorageFile_t f){Trace(38,f,0);FsOwner();return Fail(38)?SYS_ERR_IO:SYS_OK;}
SysResult_t StorageFS_Close(StorageFile_t f){Trace(39,f,0);FsOwner();if(Fail(39))return SYS_ERR_IO;if(tamper_filename){g_storage_status.file_number=99999;tamper_filename=false;}fs_open=0;return SYS_OK;}
SysResult_t StorageFS_GetSize(StorageFile_t f,uint32_t*n){Trace(40,f,0);FsOwner();*n=fs_size;return SYS_OK;}
SysResult_t StorageFS_Seek(StorageFile_t f,uint32_t n){Trace(41,f,n);FsOwner();if(Fail(41))return SYS_ERR_IO;fs_position=n;return SYS_OK;}

//通过真实公开接口推进帧接收和归还
static void Process(void){owner=RUNTIME_TARGET_STORAGE;App_StorageProcess();Trace(90,0,0);}
static void Returns(void){owner=RUNTIME_TARGET_CAMERA;App_StorageProcessReturns();Trace(91,0,0);owner=RUNTIME_TARGET_STORAGE;}
static SysResult_t Offer(bool split,bool invalid)
{CameraFrame_t f;AppFrameMeta_t m;memset(&f,0,sizeof(f));memset(&m,0,sizeof(m));f.width=160;f.height=120;f.format=CAMERA_PIXEL_FORMAT_JPEG;f.data=jpeg_data;f.size_bytes=sizeof(jpeg_data);if(split){f.size_bytes2=sizeof(jpeg_data)-600;f.data2=jpeg_data+600;}if(invalid)f.size_bytes=4;m.frame_id=retain_count+1;m.timestamp_ms=tick;m.stream_epoch=1;m.width=160;m.height=120;owner=RUNTIME_TARGET_CAMERA;{SysResult_t r=App_StorageOfferFrame(&f,&m);owner=RUNTIME_TARGET_STORAGE;return r;}}

//同一入口在两种真实实现中运行，正常诊断条件下逐记录比较
uint32_t B3_TestRun(uint32_t id)
{
 AppStorageStatus_t view;SysResult_t r;case_id=id;tick=100;
 if(id==0){CHECK(App_StoragePrepare(NULL)==SYS_ERR_INVALID_STATE);return 0;}
 if(id==1){storage_config.record_fps=0;CHECK(App_StoragePrepare(&storage_config)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==2){CHECK(App_StorageStartRecording()==SYS_ERR_INVALID_STATE);return 0;}
 CHECK(App_StoragePrepare(&storage_config)==SYS_OK);
 if(id==3){CHECK(App_StoragePrepare(&storage_config)==SYS_ERR_INVALID_STATE);return 0;}
 if(id==4){uint32_t i;for(i=0;i<4;i++)CHECK(App_StorageStartRecording()==SYS_OK);CHECK(App_StorageStop()==SYS_ERR_NO_RESOURCE);for(i=0;i<4;i++)Process();CHECK(s_state==STORAGE_VIDEO);return 0;}
 if(id==5){config.camera.format=CAMERA_PIXEL_FORMAT_YUV422;CHECK(App_StorageStartRecording()==SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==6){config.output=VIDEO_OUTPUT_USB_TEST;CHECK(App_StorageRequestSnapshot()==SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==7){exception=1;CHECK(App_StorageStop()==SYS_ERR_INVALID_STATE);exception=0;CHECK(App_StorageGetStatus(NULL)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==8)fail_call=30;
 if(id==9)fail_call=31;
 if(id==10){CHECK(App_StorageRecover()==SYS_OK);Process();CHECK(g_storage_status.result==SYS_ERR_BUSY);return 0;}
 if(id==11){CHECK(App_StorageStop()==SYS_OK);Process();CHECK(s_state==STORAGE_IDLE);return 0;}
 if(id==12){CHECK(App_StorageStartRecording()==SYS_OK);CHECK(App_StorageStop()==SYS_OK);CHECK(App_StorageRequestSnapshot()==SYS_OK);CHECK(App_StorageRecover()==SYS_OK);Process();Process();Process();Process();return 0;}
 if(id==13||id==14||id==15||id==16||id==17||id==18)r=App_StorageRequestSnapshot();else r=App_StorageStartRecording();
 CHECK(r==SYS_OK);Process();
 if(id==8||id==9){CHECK(s_state==STORAGE_FAULT);fail_call=0;CHECK(App_StorageRecover()==SYS_OK);Process();CHECK(s_state==STORAGE_IDLE);return 0;}
 if(id==19){tick+=5000;Process();CHECK(s_state==STORAGE_FAULT&&g_storage_status.stop_reason==6);return 0;}
 if(id==20){CHECK(App_StorageStartRecording()==SYS_OK);Process();CHECK(g_storage_status.result==SYS_ERR_BUSY);return 0;}
 if(id==21){CHECK(App_StorageStop()==SYS_OK);CHECK(Offer(false,false)==SYS_ERR_BUSY);Process();return 0;}
 if(id==22){s_slots[0].generation=UINT32_MAX;CHECK(Offer(false,false)==SYS_ERR_NO_RESOURCE);return 0;}
 if(id==23){retain_error=SYS_ERR_IO;CHECK(Offer(false,false)==SYS_ERR_IO);CHECK(!sd_owned);return 0;}
 if(id==24){CHECK(Offer(false,false)==SYS_OK);CHECK(Offer(false,false)==SYS_ERR_BUSY);Process();Returns();return 0;}
 CHECK(Offer(id==14||id==18,id==25)==SYS_OK);
 if(id==15)corrupt_read=true;
 if(id==16)fail_call=37;
 if(id==17)fail_call=39;
 if(id==26)fail_call=35;
 if(id==27){fail_call=36;fail_nth=2;}
 if(id==28)short_write=true;
 if(id==29){tick=1200;fail_call=38;}
 if(id==30)s_queue[s_head].session++;
 if(id==31)s_queue[s_head].generation++;
 if(id==32)CHECK(App_StorageStop()==SYS_OK);
 if(id==33)release_error=SYS_ERR_IO;
 Process();
 if(id==13||id==14||id==18){CHECK(g_storage_status.saved_frames==1&&g_storage_status.saved_files==1);CHECK(sd_owned&&s_state==STORAGE_IDLE);}
 if(id==25||id==26||id==27||id==28||id==29||id==15||id==16||id==17)CHECK(s_state==STORAGE_FAULT);
 if(id==30||id==31){CHECK(s_state==STORAGE_FAULT);return 0;}
 if(id==34){owner=RUNTIME_TARGET_STORAGE;App_StorageProcessReturns();CHECK(sd_owned);}
 Returns();
 if(id==33){CHECK(sd_owned);release_error=SYS_OK;Returns();CHECK(!sd_owned);}
 if(id==35){Returns();CHECK(release_count==1);}
 if(id==36){CHECK(App_StorageGetStatus(&view)==SYS_OK);CHECK(view.pending_returns==1);Process();CHECK(App_StorageGetStatus(&view)==SYS_OK);CHECK(view.pending_returns==0);}
 if(id==37){tick=UINT32_MAX-20; s_input_tick=tick;s_fps_tick=tick;tick=30;Process();CHECK(s_state!=STORAGE_FAULT);}
 if(id==38)fail_call=36;
 if(id==39)fail_call=39;
 if(id==40)fail_call=32;
 if(id==41)fail_call=41;
 if(s_state==STORAGE_VIDEO){CHECK(App_StorageStop()==SYS_OK);Process();}
 if(id==38||id==39||id==40||id==41)CHECK(s_state==STORAGE_FAULT);
 if(id==42){CHECK(App_StorageStop()==SYS_OK);Process();CHECK(s_state==STORAGE_IDLE);}
 CHECK(bad_owner==0);
 Trace(92,Hash(file_data,fs_size),fs_size);return 0;
}

//只在候选镜像验证诊断字段不再具备业务控制权
uint32_t B3_AuthorityRun(uint32_t id)
{
#ifndef B3_BASELINE
 AppStorageStatus_t before,after;
#endif
 tick=100;
 if(id==0)
 {
#ifndef B3_BASELINE
  CHECK(!App_StorageIsReady());g_storage_status.ready=1;CHECK(!App_StorageIsReady());
  CHECK(App_StoragePrepare(&storage_config)==SYS_OK);g_storage_status.ready=0;CHECK(App_StorageIsReady());
  CHECK(App_StorageGetStatus(&before)==SYS_OK);CHECK(App_StorageIsReady());CHECK(App_StorageGetStatus(&after)==SYS_OK);CHECK(memcmp(&before,&after,sizeof(before))==0);
#endif
  return 0;
 }
 CHECK(App_StoragePrepare(&storage_config)==SYS_OK);
 if(id==1){CHECK(App_StorageStartRecording()==SYS_OK);Process();CHECK(Offer(false,false)==SYS_OK);Process();Returns();g_storage_status.saved_frames=99999;tick=1000;Process();CHECK(g_storage_status.measured_fps_milli==1000);CHECK(s_fps_frames==1);tick+=40;CHECK(Offer(false,false)==SYS_OK);Process();CHECK(g_storage_status.saved_frames==2);}
 if(id==2){CHECK(App_StorageRequestSnapshot()==SYS_OK);Process();CHECK(Offer(true,false)==SYS_OK);tamper_filename=true;Process();CHECK(g_storage_status.saved_files==1);CHECK(g_storage_status.read_crc==g_storage_status.last_crc);}
 return 0;
}

//验证创建规则和原任务循环，不让测试进入真实调度器
uint32_t B3_TaskRun(uint32_t id)
{tick=0;case_id=id;if(id==0){CHECK(StorageTask_Create()==SYS_OK);CHECK(StorageTask_Create()==SYS_ERR_INVALID_STATE);return 0;}if(id==1){fake_task_fail=true;CHECK(StorageTask_Create()==SYS_ERR_NO_RESOURCE);return 0;}if(id==3)storage_config.record_fps=0;StorageTask_Run(NULL);return 1;}
