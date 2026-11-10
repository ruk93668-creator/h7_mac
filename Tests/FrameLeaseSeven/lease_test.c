//独立执行真实FIFO、分发、存储和AVI源码，文件介质在RAM模拟
#include "../../CM7/Media/Src/camera_mjpeg_fifo.c"
#include "../../CM7/Service/jpeg_frame.c"
#include "../../CM7/Service/mjpeg_avi.c"
#include "../../CM7/Application/app_storage.c"
#include "../../CM7/Application/app_frame_dispatcher.c"
#include "../../CM7/Application/app_uvc_bridge.c"
#include "ipc_config.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define FIXTURE ((uint8_t *)0x60000000UL)
#define FILE_DATA(i) ((uint8_t *)(0x61000000UL + (i) * 0x80000UL))
#define JPEG_BYTES 4805U

uint32_t test_ipsr, test_primask, test_basepri;
static RuntimeEventTarget_t owner;
static uint32_t now, calls, writes, mounts, recoveries, close_calls, max_write_size;
static bool timing_clock;
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

bool App_EventsIsOwner(RuntimeEventTarget_t target) { return owner == target && !test_ipsr && !test_primask && !test_basepri; }
SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t target,uint32_t events) { (void)target;(void)events; return SYS_OK; }
uint32_t Platform_TimeGetMs(void) {
 if(timing_clock)now+=5U;
 if(inject_stop&&s_offering){inject_stop=0;(void)App_StorageStop();}
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
bool CameraCapture_StreamGetFrame(CameraFrame_t *f){bool ok=CameraMjpegFifo_GetFrame(f);if(ok)media_acquired++;return ok;}
SysResult_t CameraCapture_StreamReleaseFrame(void){SysResult_t r=CameraMjpegFifo_ReleaseFrame();if(!r)media_released++;return r;}
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *p){memset(p,0,sizeof(*p));p->can_submit=!host_owned;p->frame_owned=host_owned;return SYS_OK;}
SysResult_t USB_UVC_SubmitFrame(const uint8_t *data,uint32_t size){if(owner!=RUNTIME_TARGET_USB||host_owned||size!=JPEG_BYTES||data==NULL)return SYS_ERR_INVALID_STATE;host_owned=true;host_submitted++;return SYS_OK;}
SysResult_t USB_UVC_SubmitFrameView(const VideoFrameView_t *v){if(v->span_count!=2||v->spans[1].data!=(const uint8_t *)CAMERA_MJPEG_FIFO_BASE||v->spans[1].length!=JPEG_BYTES-2000)return SYS_ERR_INVALID_ARG;view_calls++;return USB_UVC_SubmitFrame(v->spans[0].data,v->size_bytes);}
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


static SysResult_t dma_start_result;
SysResult_t BSP_DCMI_StartJpegContinuous(uint32_t *a,uint32_t *b,uint32_t n,const BspDcmiByteStreamOps_t *ops,void *ctx)
{(void)a;(void)b;(void)n;(void)ops;(void)ctx;return dma_start_result;}

//构造已完成的READY元数据，原图像写在真实FIFO地址
static void Seed(uint32_t block,bool split)
{
 MjpegFrame_t f;uint32_t offset=split?CAMERA_MJPEG_FIFO_BYTES-2000:block*CAMERA_MJPEG_BLOCK_BYTES;
 uint32_t first=split?2000:JPEG_BYTES;uint8_t *p=(uint8_t *)(CAMERA_MJPEG_FIFO_BASE+offset);
 memset(&f,0,sizeof(f));memcpy(p,FIXTURE,first);if(split)memcpy((void *)CAMERA_MJPEG_FIFO_BASE,FIXTURE+first,JPEG_BYTES-first);
 f.offset=offset;f.length=JPEG_BYTES;f.epoch=s_fifo.epoch;
 f.blocks=split?((UINT64_C(1)<<63)|1):(UINT64_C(1)<<block);
 for(block=0;block<64;block++)if(f.blocks&(UINT64_C(1)<<block)){s_fifo.blocks[block].state=MJPEG_BLOCK_SEALED;s_fifo.blocks[block].parsed=true;s_fifo.blocks[block].references++;}
 s_fifo.ready[(s_fifo.ready_head+s_fifo.ready_count)%CAMERA_MJPEG_FRAME_QUEUE]=f;s_fifo.ready_count++;
}
static uint32_t Acquire(uint32_t block,bool split,CameraFrame_t *frame)
{Seed(block,split);CHECK(CameraMjpegFifo_GetFrame(frame));return 0;}
static AppFrameMeta_t Meta(void){AppFrameMeta_t m={1,now,1,320,240};return m;}
static void Process(void){owner=RUNTIME_TARGET_STORAGE;App_StorageProcess();}
static void Returns(void){owner=RUNTIME_TARGET_CAMERA;App_StorageProcessReturns();}
static uint32_t Begin(bool snap)
{owner=RUNTIME_TARGET_STORAGE;CHECK(!App_StoragePrepare(&limits));CHECK(!(snap?App_StorageRequestSnapshot():App_StorageStartRecording()));Process();CHECK(g_storage_debug.state==(snap?2U:1U));return 0;}
static uint32_t Offer(uint32_t block,bool split)
{CameraFrame_t f;AppFrameMeta_t m=Meta();owner=RUNTIME_TARGET_CAMERA;CHECK(!Acquire(block,split,&f));CHECK(!App_StorageOfferFrame(&f,&m));CHECK(!CameraMjpegFifo_ReleaseFrame());now+=100;return 0;}

uint32_t Stage08D_TestRun(uint32_t id)
{
 CameraFrame_t f,g;CameraMjpegLease_t l[7],bad;AppFrameMeta_t m=Meta();uint32_t i,next,old;SysResult_t r;
 owner=RUNTIME_TARGET_CAMERA;CHECK(!CameraMjpegFifo_Start(&config.camera));
 if(id>=51 && id<=54){
  uint32_t base=id==54?UINT32_MAX-20U:1000U;
  limits.record_fps=25;CHECK(!Begin(false));
  now=base;CHECK(!Offer(2,false));
  now=base+5U;Process();Returns();
  CHECK(s_timing_free_valid);
  if(id==53){owner=RUNTIME_TARGET_USB;CHECK(!App_StorageWantsFrame(base+6U));CHECK(!s_timing_check_valid);}
  owner=RUNTIME_TARGET_CAMERA;now=base+8U;CHECK(!App_StorageWantsFrame(now));
  CHECK(s_timing_check_tick==now);
  if(id==52){now=base+20U;CHECK(!App_StorageWantsFrame(now));CHECK(s_timing_check_tick==base+8U);}
  now=base+40U;CHECK(App_StorageWantsFrame(now));
  if(id==52){
   CameraFrame_t invalid={0};AppFrameMeta_t meta=Meta();
   CHECK(App_StorageOfferFrame(&invalid,&meta)==SYS_ERR_INVALID_ARG);
   CHECK(!g_storage_timing_debug.free_to_check.samples);
   now=base+43U;CHECK(App_StorageWantsFrame(now));
   CHECK(s_timing_eligible_tick==base+40U);
  }
  now=base+47U;CHECK(!Offer(3,false));
  CHECK(g_storage_timing_debug.free_to_check.samples==1);
  CHECK(g_storage_timing_debug.free_to_check.last_ms==3);
  CHECK(g_storage_timing_debug.check_to_eligible.last_ms==32);
  CHECK(g_storage_timing_debug.eligible_to_offer.last_ms==7);
  CHECK(g_storage_timing_debug.next_frame_wait.last_ms==42);
  CHECK(s_count==1 && CameraMjpegFifo_HasLoan());
  return 0;
 }
 if(id==46){
  Storage_TimingRecord(&g_storage_timing_debug.validate,3);
  Storage_TimingRecord(&g_storage_timing_debug.validate,7);
  Storage_TimingRecord(&g_storage_timing_debug.validate,2);
  CHECK(g_storage_timing_debug.validate.samples==3);
  CHECK(g_storage_timing_debug.validate.total_ms==12);
  CHECK(g_storage_timing_debug.validate.average_ms==4);
  CHECK(g_storage_timing_debug.validate.max_ms==7);
  g_storage_timing_debug.validate.samples=UINT32_MAX;
  Storage_TimingRecord(&g_storage_timing_debug.validate,99);
  CHECK(g_storage_timing_debug.validate.samples==UINT32_MAX);
  CHECK(g_storage_timing_debug.validate.total_ms==12);
  CHECK(g_storage_timing_debug.validate.last_ms==99);
  return 0;
 }
 if(id>=47&&id<=50){
  config.output=VIDEO_OUTPUT_NONE;
  if(id==48)now=UINT32_MAX-150U;
  CHECK(!Begin(false));
  if(id==50){Process();CHECK(!g_storage_timing_debug.frame_process.samples);CHECK(!g_storage_timing_debug.validate.samples);return 0;}
  CHECK(!Offer(2,false));timing_clock=true;
  if(id==49)((uint8_t *)s_slots[0].frame.data)[0]=0;
  Process();
  CHECK(g_storage_timing_debug.queue_wait.samples==1);
  CHECK(g_storage_timing_debug.validate.samples==1);
  CHECK(g_storage_timing_debug.frame_process.samples==1);
  CHECK(g_storage_timing_debug.frame_process.last_ms==g_storage_debug.max_write_ms);
  if(id==49){CHECK(!g_storage_timing_debug.crc.samples&&!g_storage_timing_debug.file_write.samples);CHECK(g_storage_debug.result==SYS_ERR_PROTOCOL);return 0;}
  CHECK(g_storage_timing_debug.validate.average_ms==5);
  CHECK(g_storage_timing_debug.crc.samples==1&&g_storage_timing_debug.crc.average_ms==5);
  CHECK(g_storage_timing_debug.file_write.samples==1&&g_storage_timing_debug.file_write.average_ms>=5);
  CHECK(g_storage_timing_debug.frame_process.last_ms>=15&&g_storage_timing_debug.frame_process.last_ms<1000);
  CHECK(g_storage_timing_debug.queue_wait.last_ms<1000);
  CHECK(!g_storage_timing_debug.return_wait.samples);
  now+=7;Returns();
  CHECK(g_storage_timing_debug.return_wait.samples==1&&g_storage_timing_debug.return_wait.last_ms>=7);
  now+=200;CHECK(!Offer(3,false));
  CHECK(g_storage_timing_debug.next_frame_wait.samples==1&&g_storage_timing_debug.next_frame_wait.last_ms>=200);
  return 0;
 }

 if(id==9||id==10||id==12||id==21||id==26||id==27)config.output=VIDEO_OUTPUT_NONE;
 if(id<=5||id==14||id==15||id==16||id==23||id==31){
  CHECK(!Acquire(2,false,&f));CHECK(!CameraMjpegFifo_RetainFrame(&f,&l[0]));
  if(id==0){CHECK(s_fifo.blocks[2].references==1);CHECK(s_fifo.loans[0].usb_owned&&s_fifo.loans[0].sd_owned);return 0;}
  if(id==1){CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(CameraMjpegFifo_HasLoan());CHECK(s_fifo.blocks[2].references==1);CHECK(!CameraMjpegFifo_ReleaseLease(&l[0]));CHECK(!CameraMjpegFifo_HasLoan());CHECK(s_fifo.blocks[2].state==MJPEG_BLOCK_FREE);return 0;}
  if(id==2){CHECK(!CameraMjpegFifo_ReleaseLease(&l[0]));CHECK(s_fifo.blocks[2].references==1);CHECK(CameraMjpegFifo_HasLoan());CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  if(id==3){CHECK(CameraMjpegFifo_RetainFrame(&f,&bad)!=0);CHECK(!CameraMjpegFifo_ReleaseLease(&l[0]));CHECK(CameraMjpegFifo_ReleaseLease(&l[0])!=0);return 0;}
  if(id==4){CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(!CameraMjpegFifo_ReleaseLease(&l[0]));CameraMjpegFifo_OnStopped();CHECK(!CameraMjpegFifo_Start(&config.camera));CHECK(CameraMjpegFifo_ReleaseLease(&l[0])!=0);return 0;}
  if(id==5){CHECK(!CameraMjpegFifo_ReleaseFrame());for(i=1;i<7;i++){CHECK(!Acquire(i+2,false,&f));CHECK(!CameraMjpegFifo_RetainFrame(&f,&l[i]));CHECK(!CameraMjpegFifo_ReleaseFrame());}Seed(10,false);CHECK(!CameraMjpegFifo_GetFrame(&g));CHECK(!CameraMjpegFifo_ReleaseLease(&l[3]));CHECK(CameraMjpegFifo_GetFrame(&g));CHECK(g.data==(uint8_t *)(CAMERA_MJPEG_FIFO_BASE+10*CAMERA_MJPEG_BLOCK_BYTES));return 0;}
  if(id==14){CHECK(!CameraMjpegFifo_ReleaseFrame());s_fifo.blocks[2].references=0;CHECK(CameraMjpegFifo_ReleaseLease(&l[0])!=0);CHECK(s_fifo.loans[0].sd_owned);return 0;}
  if(id==15){s_fifo.next_block=2;CHECK(!Mjpeg_BlockComplete(BSP_DCMI_TARGET_M0,CAMERA_MJPEG_FIFO_BASE,&next,&s_fifo));CHECK(next==CAMERA_MJPEG_DROP_BASE);CHECK(s_fifo.blocks[2].references==1&&s_fifo.loans[0].sd_owned);return 0;}
  if(id==16){CameraMjpegFifo_OnStopped();CHECK(CameraMjpegFifo_HasLoan());CHECK(CameraMjpegFifo_Start(&config.camera)==SYS_ERR_BUSY);CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(!CameraMjpegFifo_ReleaseLease(&l[0]));CHECK(!CameraMjpegFifo_Start(&config.camera));return 0;}
  if(id==23){g=f;g.size_bytes--;CHECK(CameraMjpegFifo_RetainFrame(&g,&bad)!=0);return 0;}
  if(id==31){CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(!CameraMjpegFifo_ReleaseLease(&l[0]));CHECK(!Acquire(3,false,&f));CHECK(!CameraMjpegFifo_RetainFrame(&f,&l[1]));CHECK(CameraMjpegFifo_ReleaseLease(&l[0])!=0);CHECK(s_fifo.loans[0].sd_owned);return 0;}
 }
 if(id==24){CHECK(CAMERA_MJPEG_MAX_LOANS==7&&APP_STORAGE_SLOT_COUNT==1);CHECK(IPC_HEARTBEAT_PERIOD_MS==100&&IPC_HEARTBEAT_TIMEOUT_MS==1000);CHECK(CAMERA_MJPEG_REGION_END<=MJPEG_AVI_INDEX_BASE);CHECK(MJPEG_AVI_INDEX_BASE+MJPEG_AVI_INDEX_BYTES<=BSP_SDRAM_END);CHECK(0xD0000400UL+7UL*614400UL<=CAMERA_MJPEG_FIFO_BASE);return 0;}
 if(id==20){CHECK(!Acquire(2,false,&f));owner=RUNTIME_TARGET_STORAGE;CHECK(!App_StoragePrepare(&limits));owner=RUNTIME_TARGET_CAMERA;CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_BUSY);CHECK(!s_fifo.loans[0].sd_owned);return 0;}
 if(id==41){CHECK(!Begin(true));CHECK(!Offer(2,false));Process();owner=RUNTIME_TARGET_CAMERA;CHECK(!Acquire(3,false,&f));CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_BUSY);CHECK(g_storage_debug.pending_returns==1);Returns();CHECK(!CameraMjpegFifo_ReleaseFrame());return 0;}
 if(id==42){
  Seed(2,false);Seed(3,false);Seed(4,false);CHECK(CameraMjpegFifo_GetFrame(&f));
  CHECK(f.data==(void *)(CAMERA_MJPEG_FIFO_BASE+4*CAMERA_MJPEG_BLOCK_BYTES));CHECK(s_fifo.ready_count==0&&s_fifo.dropped_frames==2);
  CHECK(s_fifo.blocks[2].state==MJPEG_BLOCK_FREE&&s_fifo.blocks[3].state==MJPEG_BLOCK_FREE);CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(!CameraMjpegFifo_GetFrame(&f));return 0;
 }
 if(id==43){
  for(i=2;i<13;i++){
   memset(&s_fifo.partial,0,sizeof(s_fifo.partial));s_fifo.partial.offset=i*CAMERA_MJPEG_BLOCK_BYTES;s_fifo.partial.length=JPEG_BYTES;s_fifo.partial.blocks=UINT64_C(1)<<i;s_fifo.partial.epoch=s_fifo.epoch;
   s_fifo.blocks[i].state=MJPEG_BLOCK_SEALED;s_fifo.blocks[i].parsed=true;s_fifo.blocks[i].references=1;s_fifo.input_frames++;CHECK(Mjpeg_Publish());CHECK(s_fifo.ready_count<=6);
  }
  CHECK(s_fifo.ready_count==6&&s_fifo.dropped_frames==5);return 0;
 }
 if(id==44){
  Seed(2,false);Seed(2,false);s_fifo.ready[1].offset+=6000;
  if(6000+JPEG_BYTES>CAMERA_MJPEG_BLOCK_BYTES){s_fifo.ready[1].blocks|=UINT64_C(1)<<3;s_fifo.blocks[3].state=MJPEG_BLOCK_SEALED;s_fifo.blocks[3].parsed=true;s_fifo.blocks[3].references=1;}memcpy((void *)(CAMERA_MJPEG_FIFO_BASE+2*CAMERA_MJPEG_BLOCK_BYTES+6000),FIXTURE,JPEG_BYTES);
  CHECK(s_fifo.blocks[2].references==2);CHECK(CameraMjpegFifo_GetFrame(&f));CHECK(s_fifo.blocks[2].references==1);CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(s_fifo.blocks[2].references==0);return 0;
 }
 if(id==45){
  CHECK(!Acquire(2,false,&f));CHECK(!CameraMjpegFifo_RetainFrame(&f,&l[0]));CHECK(!CameraMjpegFifo_ReleaseFrame());Seed(3,false);Seed(4,false);CHECK(CameraMjpegFifo_GetFrame(&g));CHECK(s_fifo.blocks[2].references==1&&s_fifo.loans[0].sd_owned);CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(!CameraMjpegFifo_ReleaseLease(&l[0]));CHECK(!CameraMjpegFifo_HasLoan());return 0;
 }
 if(id==38||id==39){
  uint8_t *p=(uint8_t *)(CAMERA_MJPEG_FIFO_BASE+2*CAMERA_MJPEG_BLOCK_BYTES);
  uint32_t pad=70000U-JPEG_BYTES;
  CHECK(!Begin(true));memcpy(p,FIXTURE,2);p[2]=0xFF;p[3]=0xFE;p[4]=(uint8_t)((pad-2)>>8);p[5]=(uint8_t)(pad-2);
  memset(p+6,0x55,pad-4);memcpy(p+pad+2,FIXTURE+2,JPEG_BYTES-2);
  s_fifo.ready_count=1;s_fifo.ready[0].offset=2*CAMERA_MJPEG_BLOCK_BYTES;s_fifo.ready[0].length=id==38?70000:70001;
  s_fifo.ready[0].blocks=0;
  for(i=2;i<=(2*CAMERA_MJPEG_BLOCK_BYTES+70000-1)/CAMERA_MJPEG_BLOCK_BYTES;i++){s_fifo.ready[0].blocks|=UINT64_C(1)<<i;s_fifo.blocks[i].references=1;s_fifo.blocks[i].parsed=true;s_fifo.blocks[i].state=MJPEG_BLOCK_SEALED;}
  owner=RUNTIME_TARGET_CAMERA;CHECK(CameraMjpegFifo_GetFrame(&f));
  if(id==39){CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_INVALID_ARG);CHECK(!s_fifo.loans[0].sd_owned);return 0;}
  CHECK(!App_StorageOfferFrame(&f,&m));CHECK(!CameraMjpegFifo_ReleaseFrame());Process();CHECK(g_storage_debug.state==0&&g_storage_debug.saved_files==1&&g_storage_debug.last_size==70000&&g_storage_debug.last_crc==g_storage_debug.read_crc);Returns();CHECK(!CameraMjpegFifo_HasLoan());return 0;
 }
 if(id==40){
  CHECK(!Begin(false));CHECK(!Offer(2,false));
  owner=RUNTIME_TARGET_CAMERA;CHECK(!Acquire(10,false,&f));CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_BUSY);
  CHECK(s_count==1&&s_fifo.loans[1].usb_owned&&!s_fifo.loans[1].sd_owned);CHECK(!CameraMjpegFifo_ReleaseFrame());
  CHECK(!Acquire(11,false,&g));CHECK(g.data!=(void *)f.data);return 0;
 }
 if(id>=32){
  CHECK(!BeginBridge());CHECK(!Begin(true));Seed(2,id==37);LoanBridge();
  CHECK(host_owned&&s_fifo.loans[0].usb_owned&&s_fifo.loans[0].sd_owned);
  if(id==32||id==37){Process();CameraProcess(false);CHECK(g_storage_debug.saved_files==1);CHECK(host_owned&&s_fifo.loans[0].usb_owned&&!s_fifo.loans[0].sd_owned);ReturnBridge();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  if(id==33){ReturnBridge();CHECK(CameraMjpegFifo_HasLoan());Process();CameraProcess(false);CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  if(id==34){write_result=SYS_ERR_IO;fail_write=1;Process();CameraProcess(false);CHECK(g_storage_debug.state==4&&host_owned&&CameraMjpegFifo_HasLoan());ReturnBridge();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  if(id==35){host_cancel_result=SYS_ERR_IO;UsbProcess(false);CHECK(host_owned);Process();CameraProcess(false);CHECK(CameraMjpegFifo_HasLoan());host_cancel_result=0;ReturnBridge();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  if(id==36){owner=RUNTIME_TARGET_STORAGE;CHECK(!App_StorageStop());Process();CameraProcess(false);CHECK(g_storage_debug.state==0&&host_owned&&CameraMjpegFifo_HasLoan());ReturnBridge();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  return __LINE__;
 }
 CHECK(!Begin(id==6||id==7||id==11||id==17||id==18||id==19||id==28||id==29));
 if(id==6||id==7||id==11||id==17||id==18||id==19||id==28||id==29){
  owner=RUNTIME_TARGET_CAMERA;CHECK(!Acquire(2,id==7,&f));
  if(id==29){g=f;g.format=CAMERA_PIXEL_FORMAT_YUV422;CHECK(App_StorageOfferFrame(&g,&m)==SYS_ERR_INVALID_ARG);CHECK(!s_fifo.loans[0].sd_owned);return 0;}
  old=calls;CHECK(!App_StorageOfferFrame(&f,&m));CHECK(calls==old);CHECK(s_slots[0].frame.data==f.data&&s_slots[0].frame.data2==f.data2);
  if(id==19){CHECK(s_slots[0].crc==0&&s_slots[0].state==SLOT_QUEUED);return 0;}
  CHECK(!CameraMjpegFifo_ReleaseFrame());CHECK(CameraMjpegFifo_HasLoan());
  if(id==17)((uint8_t *)f.data)[0]=0;
  if(id==11)close_result=SYS_ERR_IO;
  if(id==28)short_write=true;
  Process();
  CHECK(g_storage_debug.pending_returns==1&&CameraMjpegFifo_HasLoan());
  if(id==11||id==17||id==28)CHECK(g_storage_debug.state==4&&g_storage_debug.result!=0);
  else{CHECK(g_storage_debug.state==0&&g_storage_debug.saved_files==1&&g_storage_debug.result==0);CHECK(g_storage_debug.last_crc==g_storage_debug.read_crc);CHECK(files[0].size==JPEG_BYTES);CHECK(!memcmp(FILE_DATA(0),FIXTURE,JPEG_BYTES));}
  Returns();CHECK(!CameraMjpegFifo_HasLoan()&&g_storage_debug.returned_frames==1);return 0;
 }
 if(id==8){CHECK(!Offer(2,false));Process();Returns();CHECK(!Offer(3,true));Process();Returns();owner=RUNTIME_TARGET_STORAGE;CHECK(!App_StorageStop());Process();CHECK(g_storage_debug.state==0&&g_storage_debug.saved_frames==2&&g_storage_debug.saved_files==1);exported_size=files[0].size;return 0;}
 if(id==9||id==10||id==12||id==21||id==26||id==27){
  for(i=0;i<1;i++)CHECK(!Offer(i+2,false));CHECK(s_count==1&&CameraMjpegFifo_HasLoan());
  if(id==9){owner=RUNTIME_TARGET_CAMERA;CHECK(!Acquire(12,false,&f));CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_BUSY);CHECK(!s_fifo.loans[1].sd_owned);CHECK(g_storage_debug.queue_high_water==1);return 0;}
  if(id==10){write_result=SYS_ERR_IO;fail_write=1;Process();CHECK(s_count==0&&g_storage_debug.state==4&&g_storage_debug.pending_returns==1);Returns();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  if(id==26||id==27){if(id==27)s_queue[0].generation++;else{write_result=SYS_ERR_IO;fail_write=1;}Process();write_result=0;owner=RUNTIME_TARGET_STORAGE;CHECK(!App_StorageRecover());Process();Returns();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
  if(id==21){Process();CHECK(s_slots[0].state==SLOT_RETURN_PENDING);owner=RUNTIME_TARGET_CAMERA;CHECK(!Acquire(12,false,&f));CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_BUSY);Returns();m.timestamp_ms=now;CHECK(App_StorageOfferFrame(&f,&m)==SYS_OK);return 0;}
  owner=RUNTIME_TARGET_STORAGE;CHECK(!App_StorageStop());for(i=0;i<1;i++){Process();Returns();}CHECK(g_storage_debug.state==0&&g_storage_debug.saved_frames==1&&!CameraMjpegFifo_HasLoan());return 0;
 }
 if(id==13){CHECK(!Offer(2,false));Process();owner=RUNTIME_TARGET_STORAGE;App_StorageProcessReturns();CHECK(g_storage_debug.pending_returns==1&&CameraMjpegFifo_HasLoan());Returns();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
 if(id==22){CHECK(!Offer(2,false));Process();Returns();owner=RUNTIME_TARGET_STORAGE;CHECK(!App_StorageStop());Process();CHECK(!App_StorageStartRecording());Process();CHECK(!Offer(3,false));Process();Returns();CHECK(g_storage_debug.session==2);return 0;}
 if(id==25){CHECK(!Acquire(2,false,&f));owner=RUNTIME_TARGET_CAMERA;inject_stop=1;CHECK(App_StorageOfferFrame(&f,&m)==SYS_ERR_INVALID_STATE);CHECK(!CameraMjpegFifo_ReleaseFrame());Returns();CHECK(!CameraMjpegFifo_HasLoan());return 0;}
 if(id==30){now=5001;Process();CHECK(g_storage_debug.state==4&&g_storage_debug.result==SYS_ERR_TIMEOUT);return 0;}
 return __LINE__;
}
