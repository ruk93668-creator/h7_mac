//原五缓冲90项断言保持不变，JPEG接口只作不可达检测
#include "stream_test_seven.inc"
static uint32_t unexpected_jpeg;
SysResult_t CameraMjpegFifo_Start(const CameraConfig_t *c){(void)c;unexpected_jpeg++;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t CameraMjpegFifo_Process(void){unexpected_jpeg++;return SYS_ERR_NOT_SUPPORTED;}
bool CameraMjpegFifo_GetFrame(CameraFrame_t *f){(void)f;unexpected_jpeg++;return false;}
SysResult_t CameraMjpegFifo_ReleaseFrame(void){unexpected_jpeg++;return SYS_ERR_NOT_SUPPORTED;}
bool CameraMjpegFifo_HasLoan(void){unexpected_jpeg++;return false;}
void CameraMjpegFifo_OnStopped(void){unexpected_jpeg++;}
void CameraMjpegFifo_GetStats(uint32_t *a,uint32_t *b){(void)a;(void)b;unexpected_jpeg++;}
int Stage02_TestRun(uint32_t id)
{
 uint32_t i,free_count=0,ready=0,used=0,idx=255;CameraFrame_t f;CameraStream_t before;
 if(id<90){int r=Stream_TestRun(id);return r!=0?r:unexpected_jpeg!=0?__LINE__:0;}
 EQ(Begin(CAMERA_PIXEL_FORMAT_YUV422),0);
 if(id==93){stream.next_sequence=UINT32_MAX-2U;for(i=0;i<4;i++)EQ(Complete(i%2),SYS_OK);EQ(stream.next_sequence,1);EQ(CameraStream_FindReplaceable(&stream),0);return Valid();}
 if(id==91){EQ(Complete(0),SYS_OK);EQ(CameraStream_GetFrame(&stream,&config,&f),true);for(i=0;i<30;i++)EQ(Complete(i%2),SYS_OK);}
 else for(i=0;i<30;i++)EQ(Complete(i%2),SYS_OK);
 for(i=0;i<CAMERA_STREAM_BUFFER_COUNT;i++){
  if(stream.buffers[i].state==CAMERA_BUFFER_FREE){free_count++;idx=i;}
  if(stream.buffers[i].state==CAMERA_BUFFER_READY)ready++;
  if(stream.buffers[i].state==CAMERA_BUFFER_IN_USE)used++;
 }
 EQ(free_count,1);EQ(ready+used,4);EQ(Valid(),0);
 if(id==90||id==94){EQ(CameraStream_GetFrame(&stream,&config,&f),true);EQ(stream.buffers[stream.in_use_index].sequence,30);EQ(CameraStream_ReleaseFrame(&stream),SYS_OK);EQ(CameraStream_GetFrame(&stream,&config,&f),false);return Valid();}
 if(id==91){EQ(used,1);EQ(ready,3);EQ(f.data[0],1);return Valid();}
 if(id==92){EQ(stream.buffers[CameraStream_FindReplaceable(&stream)].state,CAMERA_BUFFER_READY);before=stream;replace_result=SYS_ERR_IO;EQ(Complete(0),SYS_ERR_IO);SAME(stream,before);EQ(stream.buffers[idx].state,CAMERA_BUFFER_FREE);return Valid();}
 if(id==95){stream.buffers[idx].state=CAMERA_BUFFER_READY;before=stream;EQ(CameraStream_Rotate(&stream,BSP_DCMI_TARGET_M0,Replace),SYS_ERR_INVALID_STATE);SAME(stream,before);return 0;}
 return __LINE__;
}
