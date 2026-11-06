//真实Media配可控BSP，不把模拟字节当作实板图像
#define STAGE03_JPEG_MOCK_EXTERNAL
#define Stream_TestRun Stage03_HistoricalRun
#include "../CameraContextRefactor/context_test.c"
#undef Stream_TestRun

#include "synthetic_jpeg.inc"
#include "hardware_jpeg.inc"
static uint32_t jpeg_words,jpeg_received;
static SysResult_t jpeg_poll=SYS_ERR_BUSY;
static uint8_t good[]={
 0xFF,0xD8,
 0xFF,0xE0,0,6,0xFF,0xD9,0x11,0x22,
 0xFF,0xC0,0,17,8,0,120,0,160,3,1,0x22,0,2,0x11,1,3,0x11,1,
 0xFF,0xDA,0,12,3,1,0,2,0x11,3,0x11,0,63,0,
 0x11,0xFF,0,0x22,0xFF,0xD0,0x33,0xFF,0xD9
};
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *p,uint32_t n){(void)p;jpeg_words=n;return start_result;}
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *n){if(jpeg_poll==SYS_OK)*n=jpeg_received;return jpeg_poll;}
int Stage02_TestRun(uint32_t id)
{
 CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_JPEG};CameraFrame_t f;
 uint8_t *data=(uint8_t *)CAMERA_BUFFER_BASE;
 uint32_t valid=0xDEADBEEFU,i,size=sizeof(good),padded=(size+3U)&~3U;
 memcpy(data,good,size);memset(data+size,0,8);
 if(id==32){
  memcpy(data,hardware_jpeg,sizeof(hardware_jpeg));
  EQ(CameraJpeg_Validate(data,sizeof(hardware_jpeg),&cfg,&valid),SYS_OK);
  EQ(valid,1085);return 0;
 }
 if(id>=28 && id<=31){
  uint32_t extra=id-28,real_size=sizeof(jpeg_fixture)+4+extra;
  data[0]=0xFF;data[1]=0xD8;data[2]=0xFF;data[3]=0xFE;data[4]=0;data[5]=(uint8_t)(extra+2);
  memset(data+6,0,extra);memcpy(data+6+extra,jpeg_fixture+2,sizeof(jpeg_fixture)-2);
  memset(data+real_size,0,4);
  EQ(CameraJpeg_Validate(data,(real_size+3)&~3U,&cfg,&valid),SYS_OK);
  EQ(valid,real_size);return 0;
 }

 if(id==0){EQ(CameraConfig_IsVariableLength(cfg.format),true);EQ(CameraConfig_GetFrameSize(&cfg,&valid),SYS_ERR_NOT_SUPPORTED);EQ(valid,0xDEADBEEFU);return 0;}
 if(id==1){EQ(CameraJpeg_Validate(NULL,padded,&cfg,&valid),SYS_ERR_INVALID_ARG);EQ(CameraJpeg_Validate(data,padded,NULL,&valid),SYS_ERR_INVALID_ARG);EQ(CameraJpeg_Validate(data,padded,&cfg,NULL),SYS_ERR_INVALID_ARG);return 0;}
 if(id>=2 && id<=15){
  if(id==3)data[0]=0;
  if(id==4)data[size-1]=0;
  if(id==5)data[19]=0;
  if(id==6)data[12]=0xFF;
  if(id==7)data[11]=0xC2;
  if(id==8){data[size]=1;padded+=4;}
  if(id==9)padded+=4;
  if(id==10)padded=size-1;
  if(id==11){data[31]=0;data[32]=1;}
  if(id==12){data[0]=0xFF;data[1]=0xD9;}
  if(id==13){data[18]=159;}
  if(id==14){data[41]=0xFF;data[42]=0xD9;}
  if(id==15){for(i=0;i<size;i++){valid=0xDEADBEEFU;EQ(CameraJpeg_Validate(data,i,&cfg,&valid)!=SYS_OK,true);EQ(valid,0xDEADBEEFU);}return 0;}
  if(id==2 || id==8 || id==9){EQ(CameraJpeg_Validate(data,padded,&cfg,&valid),SYS_OK);EQ(valid,size);}
  else {EQ(CameraJpeg_Validate(data,padded,&cfg,&valid)!=SYS_OK,true);EQ(valid,0xDEADBEEFU);}
  return 0;
 }
 EQ(CameraCapture_Init(&cfg),SYS_OK);
 EQ(s_capture.snapshot.frame_bytes,0);EQ(s_capture.snapshot.frame_capacity,262140);
 if(id==16){EQ(CameraCapture_StartStream(),SYS_ERR_NOT_SUPPORTED);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);return 0;}
 if(id==17)start_result=SYS_ERR_IO;
 EQ(CameraCapture_Start(),id==17?SYS_ERR_IO:SYS_OK);
 EQ(jpeg_words,65535);
 if(id==17){EQ(CameraCapture_GetFrame(&f),false);EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);return 0;}
 if(id==18){EQ(CameraCapture_Start(),SYS_ERR_BUSY);EQ(CameraCapture_StartStream(),SYS_ERR_BUSY);return 0;}
 if(id==19){tick=999;CameraCapture_Process();EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);tick=1000;CameraCapture_Process();EQ(CameraCapture_GetLastResult(),SYS_ERR_TIMEOUT);EQ(CameraCapture_GetFrame(&f),false);return 0;}
 if(id==20){tick=UINT32_MAX-10;s_capture.snapshot.start_tick=tick;tick+=1000;CameraCapture_Process();EQ(CameraCapture_GetLastResult(),SYS_ERR_TIMEOUT);return 0;}
 if(id==21 || id==22){jpeg_poll=id==21?SYS_ERR_NO_RESOURCE:SYS_ERR_IO;CameraCapture_Process();EQ(CameraCapture_GetLastResult(),jpeg_poll);EQ(CameraCapture_GetFrame(&f),false);return 0;}
 jpeg_poll=SYS_OK;jpeg_received=padded;
 if(id==23)data[size-1]=0;
 if(id==24)jpeg_received=262144;
 if(id==25){jpeg_poll=SYS_ERR_TIMEOUT;stop_result=SYS_ERR_IO;}
 CameraCapture_Process();
 if(id==23 || id==24 || id==25){EQ(CameraCapture_GetFrame(&f),false);EQ(CameraCapture_GetLastResult(),id==25?SYS_ERR_TIMEOUT:SYS_ERR_PROTOCOL);if(id==25)EQ(s_capture.state,CAMERA_CAPTURE_ERROR);return 0;}
 EQ(CameraCapture_GetFrame(&f),true);EQ(f.size_bytes,size);EQ(f.format,CAMERA_PIXEL_FORMAT_JPEG);EQ(f.data,data);EQ(s_capture.snapshot.received_bytes,padded);
 EQ(CameraCapture_Stop(),SYS_ERR_BUSY);EQ(CameraCapture_ReleaseFrame(),SYS_OK);EQ(CameraCapture_IsStopped(),true);
 if(id==27){EQ(CameraCapture_Start(),SYS_OK);jpeg_poll=SYS_ERR_BUSY;tick=1000;CameraCapture_Process();EQ(CameraCapture_GetFrame(&f),false);return 0;}
 return 0;
}
