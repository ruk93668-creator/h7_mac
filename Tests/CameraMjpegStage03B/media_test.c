#define Stage02_TestRun Historical_JpegRun
#include "../CameraJpegStage03A/media_test.c"
#undef Stage02_TestRun
void __aeabi_memset(void *p,size_t n,int c){volatile uint8_t *d=p;while(n--)*d++=(uint8_t)c;}
static int Receive(uint32_t extra)
{
 uint8_t *p=s_capture.snapshot.framebuffer;uint32_t n=sizeof(good)-2;
 memcpy(p,good,n);memset(p+n,0x11,extra);p[n+extra]=0xFF;p[n+extra+1]=0xD9;
 jpeg_poll=SYS_OK;jpeg_received=(sizeof(good)+extra+3)&~3U;
 memset(p+sizeof(good)+extra,0,jpeg_received-sizeof(good)-extra);
 CameraCapture_Process();return 0;
}
int Stage02_TestRun(uint32_t id)
{
 CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_JPEG};CameraFrame_t f,g;
 uint32_t i,held;uint8_t marker;
 EQ(CameraCapture_Init(&cfg),SYS_OK);
 if(id==0){cfg.width=320;EQ(CameraCapture_ValidateStreamConfig(&cfg),SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==1)start_result=SYS_ERR_IO;
 EQ(CameraCapture_StartStream(),id==1?SYS_ERR_IO:SYS_OK);
 if(id==1){EQ(s_capture.state,CAMERA_CAPTURE_ERROR);start_result=SYS_OK;EQ(CameraCapture_Stop(),SYS_OK);EQ(CameraCapture_StartStream(),SYS_OK);return 0;}
 EQ(s_capture.stream.dma_m0_index,CAMERA_BUFFER_INDEX_INVALID);
 EQ(s_capture.stream.dma_m1_index,CAMERA_BUFFER_INDEX_INVALID);
 if(id==2){EQ(CameraCapture_Start(),SYS_ERR_BUSY);EQ(CameraCapture_StartStream(),SYS_ERR_BUSY);return 0;}
 if(id==3){jpeg_poll=SYS_ERR_BUSY;CameraCapture_Process();EQ(s_capture.stream.next_sequence,0);return 0;}
 if(id==4){jpeg_poll=SYS_ERR_BUSY;tick=1000;CameraCapture_Process();EQ(CameraCapture_GetLastResult(),SYS_ERR_TIMEOUT);EQ(CameraCapture_IsStopped(),true);return 0;}
 if(id==5){jpeg_poll=SYS_ERR_NO_RESOURCE;CameraCapture_Process();EQ(CameraCapture_GetLastResult(),SYS_ERR_NO_RESOURCE);EQ(CameraCapture_StreamGetFrame(&f),false);return 0;}
 if(id==6){jpeg_poll=SYS_OK;jpeg_received=4;memset(s_capture.snapshot.framebuffer,0,4);CameraCapture_Process();EQ(CameraCapture_GetLastResult(),SYS_ERR_PROTOCOL);return 0;}
 EQ(Receive(0),0);EQ(CameraCapture_StreamGetFrame(&f),true);EQ(f.size_bytes,sizeof(good));EQ(f.format,CAMERA_PIXEL_FORMAT_JPEG);
 EQ(CameraCapture_GetFrame(&g),false);EQ(CameraCapture_StreamGetFrame(&g),false);
 held=s_capture.stream.in_use_index;marker=f.data[0];
 if(id==7){EQ(CameraCapture_Stop(),SYS_ERR_BUSY);EQ(CameraCapture_IsStopped(),false);return 0;}
 if(id==8){EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_StreamReleaseFrame(),SYS_ERR_INVALID_STATE);return 0;}
 if(id==9 || id==10 || id==11){
  jpeg_poll=SYS_ERR_IO;if(id==11)stop_result=SYS_ERR_TIMEOUT;
  CameraCapture_Process();EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);EQ(CameraCapture_IsStopped(),false);
  EQ(s_capture.stream.buffers[held].state,CAMERA_BUFFER_IN_USE);EQ(f.data[0],marker);
  if(id==11){EQ(s_capture.state,CAMERA_CAPTURE_ERROR);stop_result=SYS_OK;EQ(CameraCapture_Stop(),SYS_OK);}
  EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(CameraCapture_StreamGetFrame(&g),false);
  EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_IsStopped(),true);
  if(id==10){EQ(CameraCapture_StartStream(),SYS_OK);EQ(Receive(3),0);EQ(CameraCapture_StreamGetFrame(&g),true);EQ(g.size_bytes,sizeof(good)+3);}
  return 0;
 }
 if(id==12){start_result=SYS_ERR_IO;EQ(Receive(1),0);EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(f.data[0],marker);return 0;}
 if(id==13){s_capture.stream.next_sequence=UINT32_MAX;EQ(Receive(0),0);EQ(CameraCapture_GetLastResult(),SYS_ERR_NO_RESOURCE);return 0;}
 for(i=0;i<30;i++){EQ(Receive(i%7),0);EQ(f.data[0],marker);EQ(s_capture.stream.in_use_index,held);EQ(s_capture.stream.buffers[held].valid_bytes,sizeof(good));}
 EQ(s_capture.stream.dropped_frames,29);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_StreamGetFrame(&g),true);EQ(g.size_bytes,sizeof(good)+29%7);
 if(id==14){EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_Stop(),SYS_OK);EQ(CameraCapture_Start(),SYS_OK);return 0;}
 if(id==15){EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_Stop(),SYS_OK);EQ(CameraCapture_StartStream(),SYS_OK);EQ(s_capture.stream.next_sequence,0);return 0;}
 EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_Stop(),SYS_OK);return 0;
}
