//真实Media通过可控Continuous字节流接收，不使用Snapshot桩模拟流
#define JPEG_CONTINUOUS_EXTERNAL_MOCK
#define Stage02_TestRun HistoricalJpeg
#include "../CameraJpegStage03A/media_test.c"
#undef Stage02_TestRun
#include "../CameraMjpegStage03B/jpeg_fixture.inc"
static uint8_t input[32768];
static uint32_t input_read,input_write,frame_count,stream_starts;
static uint32_t read_limit=256;
static SysResult_t read_result=SYS_OK;
SysResult_t BSP_DCMI_StartJpegStream(uint32_t *ring,uint32_t words)
{
 EQ((uint32_t)ring,0xD0300000);EQ(words,32768);stream_starts++;return start_result;
}
SysResult_t BSP_DCMI_ReadJpegStream(uint8_t *p,uint32_t capacity,uint32_t *count,uint32_t *frames)
{
 uint32_t n=input_write-input_read;
 *count=0;*frames=frame_count;
 if(read_result!=SYS_OK)return read_result;
 if(n>capacity)n=capacity;if(n>read_limit)n=read_limit;
 memcpy(p,input+input_read,n);input_read+=n;*count=n;return SYS_OK;
}
static void Append(bool event)
{
 uint32_t n=sizeof(mjpeg_fixture);
 memcpy(input+input_write,mjpeg_fixture,n);input_write+=n;
 while(input_write%4)input[input_write++]=0;
 if(event)frame_count++;
}
static void Pump(void){uint32_t n;for(n=0;n<10;n++)CameraCapture_Process();}
int Stage02_TestRun(uint32_t id)
{
 CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_JPEG};CameraFrame_t f,g;uint32_t i,held;
 EQ(CameraCapture_Init(&cfg),SYS_OK);
 if(id==0)start_result=SYS_ERR_IO;
 EQ(CameraCapture_StartStream(),id==0?SYS_ERR_IO:SYS_OK);
 if(id==0){EQ(s_capture.state,CAMERA_CAPTURE_ERROR);return 0;}
 EQ(stream_starts,1);EQ(jpeg_words,0);
 if(id==1){EQ(CameraCapture_Start(),SYS_ERR_BUSY);EQ(CameraCapture_StartStream(),SYS_ERR_BUSY);return 0;}
 if(id==2)read_limit=1;
 Append(id!=3);Pump();
 if(id==3){EQ(CameraCapture_StreamGetFrame(&f),false);frame_count++;Pump();}
 EQ(CameraCapture_StreamGetFrame(&f),true);EQ(f.size_bytes,sizeof(mjpeg_fixture));EQ(memcmp(f.data,mjpeg_fixture,f.size_bytes),0);
 EQ(s_capture.stream.next_sequence,1);EQ(stream_starts,1);
 held=s_capture.stream.in_use_index;
 if(id==4){EQ(CameraCapture_Stop(),SYS_ERR_BUSY);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_Stop(),SYS_OK);EQ(CameraCapture_IsStopped(),true);return 0;}
 if(id==5 || id==6){read_result=id==5?SYS_ERR_NO_RESOURCE:SYS_ERR_IO;Pump();EQ(CameraCapture_GetLastResult(),read_result);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(memcmp(f.data,mjpeg_fixture,f.size_bytes),0);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_IsStopped(),true);return 0;}
 if(id==7){tick=1000;Pump();EQ(CameraCapture_GetLastResult(),SYS_ERR_TIMEOUT);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);return 0;}
 if(id==8){read_result=SYS_ERR_IO;stop_result=SYS_ERR_TIMEOUT;Pump();EQ(s_capture.state,CAMERA_CAPTURE_ERROR);EQ(CameraCapture_IsStopped(),false);stop_result=SYS_OK;EQ(CameraCapture_Stop(),SYS_OK);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_IsStopped(),true);return 0;}
 if(id==9){input[input_write++]=0x12;frame_count++;Pump();EQ(CameraCapture_GetLastResult(),SYS_ERR_PROTOCOL);return 0;}
 if(id==10){input[input_write++]=0xFF;input[input_write++]=0xD8;input[input_write++]=0;Pump();EQ(CameraCapture_GetLastResult(),SYS_ERR_PROTOCOL);return 0;}
 for(i=0;i<10;i++) {input_read=input_write=0;Append(true);Pump();EQ(s_capture.stream.in_use_index,held);EQ(memcmp(f.data,mjpeg_fixture,f.size_bytes),0);}
 EQ(stream_starts,1);EQ(s_capture.stream.next_sequence,11);EQ(s_capture.stream.dropped_frames,9);
 EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_StreamGetFrame(&g),true);EQ(g.size_bytes,1113);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);
 if(id==11){EQ(CameraCapture_Stop(),SYS_OK);EQ(CameraCapture_Start(),SYS_OK);EQ(jpeg_words,65535);return 0;}
 EQ(CameraCapture_Stop(),SYS_OK);return 0;
}
