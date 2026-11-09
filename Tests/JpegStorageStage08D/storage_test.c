#include "../../CM7/Service/jpeg_frame.c"
#include "../../CM7/Service/mjpeg_avi.c"
#include "../../CM7/Application/app_storage.c"
#include "../../CM7/Application/app_frame_dispatcher.c"

#include "../../CM7/Application/app_uvc_bridge.c"

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define FIXTURE ((uint8_t *)0x60000000UL)
#define FILE_DATA(i) ((uint8_t *)(0x61000000UL + (i) * 0x80000UL))
#define JPEG_BYTES 4805U

uint32_t test_ipsr, test_primask, test_basepri;
static RuntimeEventTarget_t owner;
static uint32_t now, calls, writes, mounts, recoveries, close_calls, max_write_size;
static SysResult_t mount_result, write_result, sync_result, close_result, unmount_result, recover_result;
static uint32_t fail_write, inject_stop, all_exists;
static bool short_write, bad_read, fsfault;
static uint64_t free_bytes = 8UL * 1024UL * 1024UL;
static VideoConfig_t config = { {320U,240U,CAMERA_PIXEL_FORMAT_JPEG}, VIDEO_OUTPUT_USB_UVC, 10U,70000U };
static AppStorageConfig_t limits = { 10U,131072U,60000U,1048576U,524288U,16U };

typedef struct { char name[17]; uint32_t size,pos; bool used,opened; } FakeFile;
static FakeFile files[32];
static bool mounted;
uint32_t exported_size, exported_file;

bool App_EventsIsOwner(RuntimeEventTarget_t target) { return owner == target && !test_ipsr; }
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target,uint32_t events) { (void)target;(void)events; return SYS_OK; }
uint32_t Platform_TimeGetMs(void) {
 if(inject_stop&&s_copying){inject_stop=0;(void)App_StorageStop();}
 return now;
}
const VideoConfig_t *VideoConfig_GetDefault(void) { return &config; }
const AppStorageConfig_t *VideoConfig_GetStorage(void) { return &limits; }
SysResult_t VideoConfig_RequestUpdate(const VideoConfig_t *c) { (void)c; return SYS_ERR_NOT_SUPPORTED; }

SysResult_t StorageFS_Mount(bool writable,uint8_t partition) {
 (void)writable;(void)partition;calls++;mounts++;
 if(mount_result)return mount_result;mounted=true;return SYS_OK;
}
SysResult_t StorageFS_Unmount(void) {
 uint32_t i;calls++;if(unmount_result)return unmount_result;
 for(i=0;i<32;i++)if(files[i].opened)return SYS_ERR_BUSY;
 mounted=false;return SYS_OK;
}
SysResult_t StorageFS_Recover(void) {
 uint32_t i;calls++;recoveries++;if(!fsfault)return SYS_ERR_INVALID_STATE;if(recover_result)return recover_result;
 for(i=0;i<32;i++)files[i].opened=false;
 fsfault=false;mounted=false;return SYS_OK;
}
SysResult_t StorageFS_Open(const char *name,uint32_t mode,StorageFile_t *handle) {
 uint32_t i;calls++;if(!mounted||fsfault)return SYS_ERR_INVALID_STATE;
 for(i=0;i<32;i++)if(files[i].used&&!strcmp(files[i].name,name))break;
 if(mode&STORAGE_FS_OPEN_CREATE_NEW) {
  if(i<32||all_exists)return SYS_ERR_ALREADY_EXISTS;
  for(i=0;i<32&&files[i].used;i++);if(i==32)return SYS_ERR_NO_RESOURCE;
  files[i].used=true;strcpy(files[i].name,name);files[i].size=0;
 } else if(i==32)return SYS_ERR_NOT_FOUND;
 files[i].pos=0;files[i].opened=true;*handle=i+1;return SYS_OK;
}
SysResult_t StorageFS_Close(StorageFile_t f) {
 calls++;close_calls++;if(close_result)return close_result;
 if(!f||f>32||!files[f-1].opened)return SYS_ERR_INVALID_STATE;
 files[f-1].opened=false;return SYS_OK;
}
SysResult_t StorageFS_Write(StorageFile_t f,const void *data,uint32_t size,uint32_t *n) {
 FakeFile *p;calls++;writes++;if(size>max_write_size)max_write_size=size;*n=0;
 if(fsfault||!mounted||!f||f>32||!files[f-1].opened)return SYS_ERR_INVALID_STATE;
 if(write_result&&writes>=fail_write){fsfault=true;return write_result;}
 p=&files[f-1];if(size>0x80000UL-p->pos)return SYS_ERR_NO_RESOURCE;
 if(short_write&&size)size--;
 memcpy(FILE_DATA(f-1)+p->pos,data,size);p->pos+=size;if(p->pos>p->size)p->size=p->pos;*n=size;return SYS_OK;
}
SysResult_t StorageFS_Read(StorageFile_t f,void *data,uint32_t size,uint32_t *n) {
 FakeFile *p;calls++;if(!f||f>32||fsfault)return SYS_ERR_IO;p=&files[f-1];
 if(size>p->size-p->pos)size=p->size-p->pos;
 memcpy(data,FILE_DATA(f-1)+p->pos,size);if(bad_read&&size)((uint8_t *)data)[0]^=1;
 p->pos+=size;*n=size;return SYS_OK;
}
SysResult_t StorageFS_Sync(StorageFile_t f){(void)f;calls++;return sync_result;}
SysResult_t StorageFS_Seek(StorageFile_t f,uint32_t offset){calls++;if(!f||f>32)return SYS_ERR_INVALID_ARG;files[f-1].pos=offset;return SYS_OK;}
SysResult_t StorageFS_GetSize(StorageFile_t f,uint32_t *n){calls++;if(!f||f>32)return SYS_ERR_INVALID_ARG;*n=files[f-1].size;return SYS_OK;}
SysResult_t StorageFS_GetSpace(uint64_t *total,uint64_t *space){calls++;*total=8UL*1024UL*1024UL;*space=free_bytes;return SYS_OK;}
SysResult_t StorageFS_GetDiagnostics(StorageFsDiagnostics_t *d){memset(d,0,sizeof(*d));d->state=fsfault?3:mounted?2:1;d->block_result=fsfault?SYS_ERR_IO:SYS_OK;return SYS_OK;}

static bool media_held, host_owned, media_split;
static uint32_t view_calls;
static uint32_t media_acquired, media_released, host_submitted;
static SysResult_t host_cancel_result;
static AppMessage_t messages[APP_CHANNEL_COUNT][4];
static uint32_t message_head[APP_CHANNEL_COUNT],message_count[APP_CHANNEL_COUNT];
bool App_MessagesIsOwner(RuntimeEventTarget_t target){return App_EventsIsOwner(target);}
SysResult_t App_MessagesSend(AppChannel_t channel,const AppMessage_t *m){if(message_count[channel]==4)return SYS_ERR_NO_RESOURCE;messages[channel][(message_head[channel]+message_count[channel])%4]=*m;message_count[channel]++;return SYS_OK;}
bool App_MessagesReceive(AppChannel_t channel,AppMessage_t *m){if(!message_count[channel])return false;*m=messages[channel][message_head[channel]];message_head[channel]=(message_head[channel]+1)%4;message_count[channel]--;return true;}
bool CameraCapture_StreamGetFrame(CameraFrame_t *f){if(owner!=RUNTIME_TARGET_CAMERA||media_held)return false;f->width=320;f->height=240;f->format=CAMERA_PIXEL_FORMAT_JPEG;f->data=FIXTURE;f->size_bytes=JPEG_BYTES;f->data2=NULL;f->size_bytes2=0;if(media_split){memcpy(FIXTURE+0x20000,FIXTURE+JPEG_BYTES-1000,1000);f->data2=FIXTURE+0x20000;f->size_bytes2=1000;}media_held=true;media_acquired++;return true;}
SysResult_t CameraCapture_StreamReleaseFrame(void){if(owner!=RUNTIME_TARGET_CAMERA||!media_held)return SYS_ERR_INVALID_STATE;media_held=false;media_released++;return SYS_OK;}
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *p){memset(p,0,sizeof(*p));p->can_submit=!host_owned;p->frame_owned=host_owned;return SYS_OK;}
SysResult_t USB_UVC_SubmitFrame(const uint8_t *data,uint32_t size){if(owner!=RUNTIME_TARGET_USB||host_owned||size!=JPEG_BYTES||data!=FIXTURE)return SYS_ERR_INVALID_STATE;host_owned=true;host_submitted++;return SYS_OK;}
SysResult_t USB_UVC_SubmitFrameView(const VideoFrameView_t *v){if(v->span_count!=2||v->spans[1].data!=FIXTURE+0x20000||v->spans[1].length!=1000)return SYS_ERR_INVALID_ARG;view_calls++;return USB_UVC_SubmitFrame(v->spans[0].data,v->size_bytes);}
SysResult_t USB_UVC_CancelFrame(void){return host_cancel_result;}
SysResult_t USB_UVC_ValidateConfig(const VideoStreamConfig_t *c){(void)c;return SYS_OK;}
SysResult_t USB_UVC_Configure(const VideoStreamConfig_t *c){(void)c;return SYS_OK;}
SysResult_t USB_UVC_Register(void){return SYS_OK;}
SysResult_t USB_Mode_SelectBootMode(UsbMode_t m,SysResult_t (*reg)(void)){(void)m;(void)reg;return SYS_OK;}
static void CameraProcess(bool enabled){owner=RUNTIME_TARGET_CAMERA;App_UvcBridgeCameraProcess(enabled);}
static void UsbProcess(bool enabled){owner=RUNTIME_TARGET_USB;App_UvcBridgeUsbProcess(enabled);}
static uint32_t BeginBridge(void){CHECK(App_UvcBridgeConfigure(&config.camera)==SYS_OK);owner=RUNTIME_TARGET_CAMERA;CHECK(App_UvcBridgeCameraBegin()==SYS_OK);return 0;}
static void LoanBridge(void){UsbProcess(true);CameraProcess(true);UsbProcess(true);}
static void ReturnBridge(void){host_owned=false;UsbProcess(false);CameraProcess(false);UsbProcess(false);}

static CameraFrame_t Frame(void) {
 CameraFrame_t f={320,240,CAMERA_PIXEL_FORMAT_JPEG,FIXTURE,JPEG_BYTES,NULL,0};return f;
}
static AppFrameMeta_t Meta(void) { AppFrameMeta_t m={1,now,1,320,240};return m; }
static SysResult_t Offer(bool split) {
 CameraFrame_t f=Frame();AppFrameMeta_t m=Meta();SysResult_t result;owner=RUNTIME_TARGET_CAMERA;
 if(split){memcpy(FIXTURE+0x20000,FIXTURE+JPEG_BYTES-1000,1000);f.size_bytes2=1000;f.data2=FIXTURE+0x20000;}
 result=App_StorageOfferFrame(&f,&m);owner=RUNTIME_TARGET_STORAGE;return result;
}
static void Process(void){owner=RUNTIME_TARGET_STORAGE;App_StorageProcess();}
static uint32_t Start(bool snapshot){CHECK(App_StoragePrepare(&limits)==SYS_OK);CHECK((snapshot?App_StorageRequestSnapshot():App_StorageStartRecording())==SYS_OK);Process();CHECK(g_storage_debug.state==(snapshot?2U:1U));return 0;}

uint32_t Stage08D_TestRun(uint32_t id) {
 CameraFrame_t f=Frame();AppFrameMeta_t m=Meta();MjpegAvi_t a;StorageFile_t file;
 uint32_t i,before,token;AppStorageStatus_t status;SysResult_t rr;
 owner=RUNTIME_TARGET_STORAGE;
 if(id==0){CHECK(JpegFrame_Validate(&f)==SYS_OK);return 0;}
 if(id==1){memcpy(FIXTURE+0x20000,FIXTURE+1,JPEG_BYTES-1);f.size_bytes2=JPEG_BYTES-1;f.data2=FIXTURE+0x20000;CHECK(JpegFrame_Validate(&f)==SYS_OK);return 0;}
 if(id==2){f.size_bytes--;CHECK(JpegFrame_Validate(&f)==SYS_ERR_PROTOCOL);return 0;}
 if(id==3){f.size_bytes2=f.size_bytes;CHECK(JpegFrame_Validate(&f)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==4){f.width=160;CHECK(JpegFrame_Validate(&f)==SYS_ERR_PROTOCOL);return 0;}
 if(id==5){f.format=CAMERA_PIXEL_FORMAT_YUV422;CHECK(JpegFrame_Validate(&f)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==6){CHECK(App_StoragePrepare(&limits)==SYS_OK);Process();CHECK(calls==0);CHECK(!App_StorageWantsFrame(0));return 0;}
 if(id==7||id==8){CHECK(Start(true)==0);CHECK(Offer(id==8)==SYS_OK);Process();CHECK(g_storage_debug.state==0&&g_storage_debug.saved_files==1);CHECK(!strcmp(files[0].name,"0:/IMG00001.JPG"));CHECK(files[0].size==JPEG_BYTES);CHECK(g_storage_debug.last_crc==g_storage_debug.read_crc);CHECK(!memcmp(FILE_DATA(0),FIXTURE,JPEG_BYTES));return 0;}
 if(id==9){CHECK(Start(false)==0);for(i=0;i<4;i++){now+=100;CHECK(Offer(false)==SYS_OK);}now+=100;CHECK(Offer(false)==SYS_ERR_NO_RESOURCE);CHECK(s_count==4&&g_storage_debug.dropped_frames==1);CHECK(s_slots[0].state==SLOT_QUEUED);return 0;}
 if(id==10){CHECK(Start(false)==0);now=100;CHECK(Offer(false)==SYS_OK);CHECK(Offer(false)==SYS_ERR_BUSY);return 0;}
 if(id==11){CHECK(Start(false)==0);f.size_bytes=70001;owner=RUNTIME_TARGET_CAMERA;CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_INVALID_ARG);CHECK(s_copying==0);return 0;}
 if(id==12){CHECK(App_StoragePrepare(&limits)==SYS_OK);for(i=0;i<4;i++)CHECK(App_StorageStop()==SYS_OK);CHECK(App_StorageStop()==SYS_ERR_NO_RESOURCE);CHECK(s_command_count==4);return 0;}
 if(id==13){CHECK(App_StoragePrepare(&limits)==SYS_OK);test_ipsr=16;CHECK(App_StorageRequestSnapshot()==SYS_ERR_INVALID_STATE);CHECK(calls==0);return 0;}
 if(id==14){config.camera.format=CAMERA_PIXEL_FORMAT_YUV422;CHECK(App_StoragePrepare(&limits)==SYS_OK);CHECK(App_StorageRequestSnapshot()==SYS_ERR_NOT_SUPPORTED);CHECK(calls==0);return 0;}
 if(id==15){CHECK(App_StoragePrepare(&limits)==SYS_OK);mount_result=SYS_ERR_NOT_FOUND;CHECK(App_StorageStartRecording()==SYS_OK);Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_NOT_FOUND);return 0;}
 if(id==16){free_bytes=1;CHECK(App_StoragePrepare(&limits)==SYS_OK);CHECK(App_StorageStartRecording()==SYS_OK);Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_NO_RESOURCE);return 0;}
 if(id==17||id==18){CHECK(Start(true)==0);CHECK(Offer(false)==SYS_OK);if(id==17){write_result=SYS_ERR_TIMEOUT;fail_write=1;}else short_write=true;Process();CHECK(g_storage_debug.state==4);CHECK(g_storage_debug.saved_files==0);CHECK(s_slots[0].state==SLOT_FREE);return 0;}
 if(id==19){CHECK(Start(true)==0);CHECK(Offer(false)==SYS_OK);sync_result=SYS_ERR_IO;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_IO);return 0;}
 if(id==20){CHECK(Start(true)==0);CHECK(Offer(false)==SYS_OK);bad_read=true;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_PROTOCOL);return 0;}
 if(id==21){CHECK(Start(true)==0);all_exists=1;CHECK(Offer(false)==SYS_OK);Process();CHECK(g_storage_debug.state==4&&s_next_file==129);CHECK(writes==0);return 0;}
 if(id==22){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);token=s_slots[0].generation;CHECK(!Storage_Release(0,token));Process();CHECK(!Storage_Release(0,token));CHECK(g_storage_debug.saved_frames==1);return 0;}
 if(id==23){CHECK(Start(false)==0);for(i=0;i<4;i++){now+=100;CHECK(Offer(false)==SYS_OK);}CHECK(App_StorageStop()==SYS_OK);for(i=0;i<4;i++)Process();CHECK(g_storage_debug.state==0&&g_storage_debug.saved_frames==4&&g_storage_debug.saved_files==1);CHECK(s_count==0);return 0;}
 if(id==24){CHECK(Start(false)==0);CHECK(App_StorageStop()==SYS_OK);Process();CHECK(g_storage_debug.state==0&&g_storage_debug.saved_files==0);return 0;}
 if(id==25){CHECK(Start(true)==0);CHECK(Offer(false)==SYS_OK);CHECK(App_StorageStartRecording()==SYS_OK);Process();CHECK(g_storage_debug.rejected_requests==1);CHECK(g_storage_debug.saved_files==1);return 0;}
 if(id==26){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);write_result=SYS_ERR_IO;fail_write=1;unmount_result=SYS_ERR_BUSY;Process();CHECK(g_storage_debug.state==4);CHECK(App_StorageRecover()==SYS_OK);unmount_result=0;recover_result=SYS_ERR_TIMEOUT;Process();CHECK(g_storage_debug.state==4);CHECK(App_StorageRecover()==SYS_OK);recover_result=0;Process();CHECK(g_storage_debug.state==0&&recoveries==2);return 0;}
 if(id==27){CHECK(Start(false)==0);s_slots[0].state=SLOT_WRITING;s_slots[0].generation=4;CHECK(!Storage_Release(0,3));CHECK(s_slots[0].state==SLOT_WRITING);CHECK(Storage_Release(0,4));CHECK(!Storage_Release(0,4));return 0;}
 if(id==28){CHECK(Start(true)==0);s_session=UINT32_MAX;s_accepting=false;Storage_Finish(SYS_OK);CHECK(App_StorageRequestSnapshot()==SYS_OK);Process();CHECK(!s_accepting);return 0;}
 if(id==29){limits.record_fps=0;CHECK(App_StoragePrepare(&limits)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==30){CHECK(App_FrameDispatcherValidateOutput(VIDEO_OUTPUT_ETH)==SYS_ERR_NOT_SUPPORTED);CHECK(App_FrameDispatcherValidateOutput(VIDEO_OUTPUT_WIFI)==SYS_ERR_NOT_SUPPORTED);CHECK(App_FrameDispatcherRequestAi()==SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==31){owner=RUNTIME_TARGET_CAMERA;CHECK(App_FrameDispatcherBegin(1)==SYS_OK);CHECK(App_FrameDispatcherPublish(&f,&m)==SYS_OK);CHECK(m.frame_id==1&&m.stream_epoch==1);CHECK(App_FrameDispatcherBegin(1)==SYS_ERR_INVALID_STATE);CHECK(App_FrameDispatcherBegin(2)==SYS_OK);CHECK(App_FrameDispatcherPublish(&f,&m)==SYS_OK&&m.frame_id==1);return 0;}
 if(id==32){CHECK(Start(false)==0);owner=RUNTIME_TARGET_USB;CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_INVALID_STATE);return 0;}
 if(id==33){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);f=Frame();owner=RUNTIME_TARGET_CAMERA;m.width=123;now=200;m.timestamp_ms=now;CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==34){CHECK(Start(true)==0);CHECK(Offer(false)==SYS_OK);unmount_result=SYS_ERR_IO;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.cleanup_result==SYS_ERR_IO&&g_storage_debug.saved_files==0&&g_storage_debug.failures==1);return 0;}
 if(id==35){CHECK(App_StoragePrepare(&limits)==SYS_OK);before=calls;CHECK(App_StorageGetStatus(&status)==SYS_OK);CHECK(calls==before);CHECK(App_StorageGetStatus(NULL)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==36){CHECK(Start(false)==0);for(i=0;i<3;i++){now=i*100;CHECK(Offer(false)==SYS_OK);Process();}CHECK(App_StorageStop()==SYS_OK);Process();CHECK(g_storage_debug.state==0);CHECK(!strcmp(files[0].name,"0:/VID00001.AVI"));exported_file=0;exported_size=files[0].size;return 0;}
 if(id==37){CHECK(Start(false)==0);for(i=0;i<60;i++){now=i*100;CHECK(Offer(false)==SYS_OK);Process();}CHECK(App_StorageStop()==SYS_OK);Process();CHECK(g_storage_debug.saved_files>=2&&g_storage_debug.state==0);return 0;}
 if(id==38){limits.max_segments=1;CHECK(Start(false)==0);for(i=0;i<40&&s_accepting;i++){now=i*100;rr=Offer(false);CHECK(rr==SYS_OK||rr==SYS_ERR_BUSY);Process();}CHECK(g_storage_debug.state==0&&g_storage_debug.saved_files==1);return 0;}
 if(id==39){CHECK(Start(false)==0);now=UINT32_MAX-50;CHECK(Offer(false)==SYS_OK);Process();now=60;CHECK(Offer(false)==SYS_OK);Process();CHECK(App_StorageStop()==SYS_OK);Process();CHECK(g_storage_debug.saved_files==1);return 0;}
 if(id>=40&&id<=46){CHECK(StorageFS_Mount(true,0)==SYS_OK);CHECK(StorageFS_Open("0:/DIRECT.AVI",STORAGE_FS_OPEN_WRITE|STORAGE_FS_OPEN_CREATE_NEW,&file)==SYS_OK);CHECK(MjpegAvi_Begin(&a,file,320,240,10)==SYS_OK);
 if(id==40){a.frames=4096;CHECK(MjpegAvi_Append(&a,FIXTURE,JPEG_BYTES,0,0x100000)==SYS_ERR_NO_RESOURCE);return 0;}
 if(id==41){before=writes;CHECK(MjpegAvi_Append(&a,FIXTURE,JPEG_BYTES,0,224)==SYS_ERR_NO_RESOURCE);CHECK(writes==before);return 0;}
 if(id==42){CHECK(MjpegAvi_Finish(&a)==SYS_ERR_INVALID_STATE);return 0;}
 if(id==43){short_write=true;CHECK(MjpegAvi_Append(&a,FIXTURE,JPEG_BYTES,0,0x100000)==SYS_ERR_NO_RESOURCE);CHECK(a.failed);return 0;}
 CHECK(MjpegAvi_Append(&a,FIXTURE,JPEG_BYTES,0,0x100000)==SYS_OK);
 if(id==44){sync_result=SYS_ERR_IO;CHECK(MjpegAvi_Finish(&a)==SYS_ERR_IO);CHECK(a.failed);return 0;}
 if(id==45){write_result=SYS_ERR_IO;fail_write=writes+1;CHECK(MjpegAvi_Finish(&a)==SYS_ERR_IO);CHECK(a.failed);return 0;}
 CHECK(MjpegAvi_Finish(&a)==SYS_OK);CHECK(!memcmp(FILE_DATA(0)+a.position,"idx1",4));CHECK(FILE_DATA(0)[224+8+JPEG_BYTES]==0);return 0;}
 if(id==47){f.size_bytes2=100;f.data2=NULL;CHECK(JpegFrame_Validate(&f)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==48){CHECK(Start(false)==0);for(i=0;i<4;i++){now+=100;CHECK(Offer(false)==SYS_OK);}CHECK(Offer(false)==SYS_ERR_BUSY);write_result=SYS_ERR_IO;fail_write=1;Process();CHECK(s_count==0&&g_storage_debug.state==4);for(i=0;i<4;i++)CHECK(s_slots[i].state==SLOT_FREE);return 0;}
 if(id==49){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);before=s_session;CHECK(App_StorageStop()==SYS_OK);Process();CHECK(App_StorageStartRecording()==SYS_OK);Process();CHECK(s_session==before+1);CHECK(!Storage_Release(0,1));return 0;}
 if(id==50){CHECK(Start(false)==0);inject_stop=1;CHECK(Offer(false)==SYS_ERR_INVALID_STATE);CHECK(s_copying==0&&s_count==0&&s_slots[0].state==SLOT_FREE);Process();CHECK(g_storage_debug.state==0);return 0;}
 if(id==51){CHECK(Start(true)==0);now=5000;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_TIMEOUT);return 0;}
 if(id==52){CHECK(Start(false)==0);f=Frame();FIXTURE[JPEG_BYTES-1]=0;owner=RUNTIME_TARGET_CAMERA;CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_PROTOCOL);CHECK(s_slots[0].state==SLOT_FREE);return 0;}
 if(id==53){CHECK(Start(false)==0);CHECK(App_StorageRequestSnapshot()==SYS_OK);Process();CHECK(g_storage_debug.rejected_requests==1&&s_mode==APP_STORAGE_VIDEO);return 0;}
 if(id==54){config.output=VIDEO_OUTPUT_USB_TEST;CHECK(App_StoragePrepare(&limits)==SYS_OK);CHECK(App_StorageRequestSnapshot()==SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==55){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);Process();CHECK(App_StorageGetStatus(&status)==SYS_OK);CHECK(status.saved_frames==1&&status.queue_depth==0);return 0;}
 if(id==56){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);Process();s_free_budget=limits.reserve_bytes;now=100;CHECK(Offer(false)==SYS_OK);Process();CHECK(g_storage_debug.state==0&&g_storage_debug.stop_reason==3&&g_storage_debug.saved_files==1);CHECK(!memcmp(FILE_DATA(0)+s_avi.position,"idx1",4));return 0;}
 if(id==57){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);Process();CHECK(App_StorageStop()==SYS_OK);Process();CHECK(MjpegAvi_Finish(&s_avi)==SYS_ERR_INVALID_STATE);CHECK(MjpegAvi_Append(&s_avi,FIXTURE,JPEG_BYTES,200,131072)==SYS_ERR_INVALID_STATE);return 0;}
 if(id==58){limits.max_segments=1;CHECK(Start(false)==0);for(i=0;i<26;i++){now=i*100;CHECK(Offer(false)==SYS_OK);Process();}for(i=0;i<4;i++){now+=100;CHECK(Offer(false)==SYS_OK);}CHECK(App_StorageStop()==SYS_OK);for(i=0;i<4;i++)Process();CHECK(g_storage_debug.state==0&&g_storage_debug.saved_files==1);CHECK(s_count==0);return 0;}
 if(id==59){CHECK(Start(false)==0);owner=RUNTIME_TARGET_CAMERA;CHECK(App_FrameDispatcherBegin(1)==SYS_OK);s_next_frame=UINT32_MAX;CHECK(!App_FrameDispatcherCanAcquire());CHECK(App_FrameDispatcherPublish(&f,&m)==SYS_ERR_NO_RESOURCE);return 0;}
 if(id==60){CHECK(Start(true)==0);CHECK(BeginBridge()==0);CameraProcess(true);CHECK(media_acquired==1&&media_released==1&&!media_held);Process();CHECK(g_storage_debug.saved_files==1&&g_storage_debug.state==0);CHECK(host_submitted==0);return 0;}
 if(id==61){CHECK(Start(false)==0);CHECK(BeginBridge()==0);LoanBridge();CHECK(host_owned&&media_held&&s_count==1);Process();CHECK(g_storage_debug.saved_frames==1&&media_held&&media_released==0);ReturnBridge();CHECK(media_released==1);return 0;}
 if(id==62){CHECK(Start(false)==0);CHECK(BeginBridge()==0);LoanBridge();write_result=SYS_ERR_IO;fail_write=1;Process();CHECK(g_storage_debug.state==4&&host_owned&&media_held);ReturnBridge();CHECK(media_released==1);return 0;}
 if(id==63){CHECK(Start(false)==0);CHECK(BeginBridge()==0);LoanBridge();host_cancel_result=SYS_ERR_IO;UsbProcess(false);CHECK(host_owned&&media_held&&media_released==0);CHECK(App_StorageStop()==SYS_OK);Process();CHECK(g_storage_debug.state==0&&host_owned&&media_held);ReturnBridge();CHECK(media_released==1);return 0;}
 if(id==64){CHECK(App_StoragePrepare(&limits)==SYS_OK);CHECK(BeginBridge()==0);LoanBridge();CHECK(host_owned&&media_held&&s_count==0&&calls==0);ReturnBridge();CHECK(media_released==1);return 0;}
 if(id==65){CHECK(Start(false)==0);CHECK(BeginBridge()==0);for(i=0;i<4;i++){now+=100;CHECK(Offer(false)==SYS_OK);}now+=100;LoanBridge();CHECK(host_owned&&media_held&&g_storage_debug.dropped_frames==1);ReturnBridge();CHECK(media_released==1);return 0;}
 if(id==66){CHECK(Start(true)==0);CHECK(BeginBridge()==0);media_split=true;LoanBridge();CHECK(host_owned&&view_calls==1&&s_count==1);Process();CHECK(g_storage_debug.saved_files==1&&files[0].size==JPEG_BYTES);CHECK(!memcmp(FILE_DATA(0),FIXTURE,JPEG_BYTES));ReturnBridge();CHECK(media_released==1);return 0;}
 if(id==67){CHECK(Start(true)==0);CHECK(BeginBridge()==0);CameraProcess(true);CHECK(!media_held&&media_released==1);token=~JpegFrame_Crc32(FIXTURE,JPEG_BYTES,UINT32_MAX);memset(FIXTURE,0,JPEG_BYTES);Process();CHECK(g_storage_debug.saved_files==1&&g_storage_debug.last_crc==token&&g_storage_debug.read_crc==token);CHECK(FILE_DATA(0)[0]==0xff&&FILE_DATA(0)[1]==0xd8);return 0;}
 if(id==68){uint8_t *large=FIXTURE+0x40000;CHECK(Start(true)==0);large[0]=0xff;large[1]=0xd8;large[2]=0xff;large[3]=0xfe;large[4]=(uint8_t)(65193U>>8);large[5]=(uint8_t)65193U;memset(large+6,0,65191);memcpy(large+65197,FIXTURE+2,JPEG_BYTES-2);f.data=large;f.size_bytes=70000;CHECK(JpegFrame_Validate(&f)==SYS_OK);owner=RUNTIME_TARGET_CAMERA;CHECK(App_StorageOfferFrame(&f,&m)==SYS_OK);Process();CHECK(g_storage_debug.saved_files==1&&files[0].size==70000);CHECK(max_write_size<=32768);CHECK(!memcmp(FILE_DATA(0),large,70000));return 0;}
 if(id==69){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);mounted=false;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.saved_files==0&&s_count==0);CHECK(s_slots[0].state==SLOT_FREE);return 0;}
 if(id==70){CHECK(App_StoragePrepare(&limits)==SYS_OK);mount_result=SYS_ERR_NOT_FOUND;CHECK(App_StorageRequestSnapshot()==SYS_OK);Process();CHECK(g_storage_debug.state==4);CHECK(App_StorageRecover()==SYS_OK);Process();CHECK(g_storage_debug.state==0&&recoveries==0);mount_result=SYS_OK;CHECK(App_StorageRequestSnapshot()==SYS_OK);Process();CHECK(g_storage_debug.state==2);return 0;}
 if(id==71){CHECK(Start(true)==0);all_exists=1;CHECK(Offer(false)==SYS_OK);Process();CHECK(g_storage_debug.state==4&&!mounted&&!fsfault);CHECK(App_StorageRecover()==SYS_OK);Process();CHECK(g_storage_debug.state==0&&recoveries==0);return 0;}
 if(id==72){CHECK(Start(true)==0);CHECK(Offer(false)==SYS_OK);unmount_result=SYS_ERR_IO;Process();CHECK(g_storage_debug.state==4&&mounted&&!fsfault);CHECK(App_StorageRecover()==SYS_OK);unmount_result=SYS_OK;Process();CHECK(g_storage_debug.state==0&&!mounted&&recoveries==0);return 0;}
 if(id==73){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);token=s_slots[0].generation;s_queue[0].generation=token+1;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_PROTOCOL&&writes==0);CHECK(App_StorageRecover()==SYS_OK);Process();CHECK(g_storage_debug.state==0&&s_slots[0].state==SLOT_FREE);CHECK(!Storage_Release(0,token));return 0;}
 if(id==74){CHECK(Start(false)==0);CHECK(Offer(false)==SYS_OK);s_queue[0].session=s_session+1;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_PROTOCOL&&writes==0);return 0;}
 return __LINE__;
}
