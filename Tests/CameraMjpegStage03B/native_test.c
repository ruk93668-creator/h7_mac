#define APP_VIDEO_MJPEG_MODE 1
#define STAGE03B_MJPEG_FIXTURE
#define Stage02_TestRun Historical_NativeRun
#include "../NativeRuntimeStage02/phase02d.c"
#undef Stage02_TestRun
#include "jpeg_fixture.inc"
static bool jpeg_pending;
static SysResult_t jpeg_result=SYS_OK;
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *p,uint32_t n)
{
 if(n!=65535 || fake_dma_active)return SYS_ERR_BUSY;
 dma_targets[0]=p;fake_dma_active=true;dcmi_starts++;return SYS_OK;
}
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *n)
{
 if(!jpeg_pending)return SYS_ERR_BUSY;
 jpeg_pending=false;
 if(jpeg_result!=SYS_OK)return jpeg_result;
 *n=(sizeof(mjpeg_fixture)+3)&~3U;fake_dma_active=false;return SYS_OK;
}
static int JpegFrame(void)
{
 memcpy(dma_targets[0],mjpeg_fixture,sizeof(mjpeg_fixture));
 jpeg_pending=true;
 test_ipsr=16U+DCMI_IRQn;
 camera_error_callback(BSP_DCMI_NOTIFY_PROGRESS,camera_error_context);
 test_ipsr=0;Cam();Usb();return 0;
}
static int JpegLoan(void)
{
 EQ(Host(),0);EQ(JpegFrame(),0);EQ(s_usb_bridge.state,BRIDGE_USB_LOANED);
 EQ(s_usb_bridge.lease.frame.size_bytes,sizeof(mjpeg_fixture));
 Sof();EQ(s_uvc_active->stream_state,USB_UVC_IN_FLIGHT);return 0;
}
static int Finish(void)
{
 uint32_t i;
 for(i=0;i<200 && g_bridge_debug.released_frames==0;i++){
  if(s_uvc_active && s_uvc_active->stream_state==USB_UVC_IN_FLIGHT)CompleteUsb();
  else Sof();
  Cam();Usb();Control();
 }
 EQ(g_bridge_debug.released_frames,1);return 0;
}
static int StopAfterComplete(void)
{
 uint32_t i;
 Select(RUNTIME_TARGET_CONTROL);EQ(ControlTask_RequestStop(),SYS_OK);Control();
 for(i=0;i<10;i++)Pump();
 EQ(s_control_state,CONTROL_IDLE);
 EQ(fake_dma_active,false);
 EQ(camera_device.base.state,DEVICE_STATE_READY);
 EQ(s_camera_bridge.held,false);
 EQ(s_usb_bridge.state,BRIDGE_USB_IDLE);return 0;
}
int Stage02_TestRun(uint32_t id)
{
 uint32_t i,index;const uint8_t *held;
 EQ(Boot(),0);EQ(s_capture.config.format,CAMERA_PIXEL_FORMAT_JPEG);
 if(id==0){EQ(JpegFrame(),0);EQ(g_bridge_debug.acquired_frames,0);EQ(s_capture.stream.next_sequence,1);return 0;}
 if(id>=11 && id<=14){
  AppMessage_t message={0};
  EQ(Host(),0);EQ(s_usb_bridge.state,BRIDGE_USB_REQUEST);
  if(id==12){
   Select(RUNTIME_TARGET_USB);message.type=APP_MESSAGE_ACK;
   while(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&message)==SYS_OK){}
   Select(RUNTIME_TARGET_CONTROL);EQ(ControlTask_RequestStop(),SYS_OK);Control();Usb();
   EQ(s_usb_bridge.state,BRIDGE_USB_REQUEST);EQ(s_usb_bridge.cancel_sent,false);
   for(i=0;i<12;i++)Pump();EQ(s_control_state,CONTROL_IDLE);return 0;
  }
  if(id==13){
   memcpy(dma_targets[0],mjpeg_fixture,sizeof(mjpeg_fixture));jpeg_pending=true;Cam();
   EQ(s_camera_bridge.held,true);EQ(s_usb_bridge.state,BRIDGE_USB_REQUEST);
  }
  if(id==14){
   Select(RUNTIME_TARGET_USB);message.type=APP_MESSAGE_FRAME_CANCEL_REQUEST;
   message.command_id=s_usb_bridge.lease.command_id+1;
   EQ(App_MessagesSend(APP_CHANNEL_TO_CAMERA,&message),SYS_OK);Cam();
   EQ(s_camera_bridge.request_pending,true);
  }
  EQ(StopAfterComplete(),0);EQ(s_camera_bridge.held,false);return 0;
 }
 EQ(JpegLoan(),0);index=s_capture.stream.in_use_index;held=s_usb_bridge.lease.frame.data;
 EQ(s_capture.stream.buffers[index].state,CAMERA_BUFFER_IN_USE);
 if(id==1){for(i=0;i<30;i++)EQ(JpegFrame(),0);EQ(memcmp(held,mjpeg_fixture,sizeof(mjpeg_fixture)),0);EQ(s_capture.stream.in_use_index,index);EQ(g_bridge_debug.acquired_frames,1);return 0;}
 if(id==2){EQ(Stop(),0);EQ(s_capture.mode,CAMERA_CAPTURE_MODE_NONE);return 0;}
 if(id==3 || id==4){
  Select(RUNTIME_TARGET_USB);
  if(id==3)HAL_PCD_ResetCallback(&s_pcd);else HAL_PCD_DisconnectCallback(&s_pcd);
  for(i=0;i<10;i++)Pump();
  EQ(g_bridge_debug.released_frames,1);EQ(s_camera_bridge.held,false);return 0;
 }
 if(id==5){Select(RUNTIME_TARGET_USB);Request(1,USB_REQ_SET_INTERFACE,0,1,0);for(i=0;i<10;i++)Pump();EQ(g_bridge_debug.released_frames,1);EQ(s_camera_bridge.held,false);return 0;}
 if(id==6){
  fault=20;Select(RUNTIME_TARGET_USB);HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);
  for(i=0;i<8;i++)Pump();EQ(s_camera_bridge.held,true);EQ(s_capture.stream.in_use_index,index);EQ(g_bridge_debug.released_frames,0);
  fault=0;for(i=0;i<10;i++)Pump();EQ(g_bridge_debug.released_frames,1);return 0;
 }
 if(id==7 || id==8){
  jpeg_result=SYS_ERR_IO;if(id==8)fake_dcmi_stop=SYS_ERR_TIMEOUT;
  EQ(JpegFrame(),0);EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);
  EQ(s_capture.stream.in_use_index,index);EQ(memcmp(held,mjpeg_fixture,sizeof(mjpeg_fixture)),0);
  fake_dcmi_stop=SYS_OK;for(i=0;i<15;i++)Pump();
  EQ(s_camera_bridge.held,false);EQ(fake_dma_active,false);EQ(camera_device.base.state,DEVICE_STATE_READY);return 0;
 }
 EQ(Finish(),0);EQ(g_bridge_debug.submitted_frames,1);EQ(s_uvc_active->completed_frames,1);
 if(id==9){
  for(i=0;i<100;i++){Sof();Cam();}
  EQ(JpegFrame(),0);
  for(i=0;i<100 && s_uvc_active->completed_frames<2;i++){
   if(s_uvc_active->stream_state==USB_UVC_IN_FLIGHT)CompleteUsb();else Sof();Cam();Usb();
  }
  for(i=0;i<5;i++)Pump();
  EQ(s_uvc_active->completed_frames,2);EQ(g_bridge_debug.released_frames,2);
 }
 if(id==10){
  EQ(StopAfterComplete(),0);
  Select(RUNTIME_TARGET_CONTROL);
  EQ(ControlTask_RequestStart(),SYS_OK);
  for(i=0;i<12;i++)Pump();
  EQ(s_control_state,CONTROL_RUNNING);
  EQ(s_capture.state,CAMERA_CAPTURE_CAPTURING);return 0;
 }
 return 0;
}
