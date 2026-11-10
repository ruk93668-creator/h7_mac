//当前APP配置贯通Device、Media、真实UVC描述符和Probe/Commit
#include "../UvcStreaming/stream_test.c"
#include "../../CM7/Application/video_config.c"
#include "ov5640_device.h"
#include "camera_capture.h"
int Stage02_TestRun(uint32_t id)
{
 const VideoConfig_t *app=VideoConfig_GetDefault();VideoStreamConfig_t cfg;
 CameraConfig_t portrait={240,320,CAMERA_PIXEL_FORMAT_JPEG};
 uint16_t length;uint8_t *desc;uint32_t off,found=0;
 cfg.camera=app->camera;cfg.fps=app->output_fps;cfg.max_frame_bytes=app->output_max_frame_bytes;
 EQ(cfg.camera.width,320);EQ(cfg.camera.height,240);EQ(cfg.camera.format,CAMERA_PIXEL_FORMAT_JPEG);EQ(cfg.fps,25);
 if(id==0){EQ(Ov5640_ValidateConfig(&cfg.camera),SYS_OK);return 0;}
 if(id==1){EQ(CameraCapture_ValidateStreamConfig(&cfg.camera),SYS_OK);return 0;}
 if(id==2){EQ(USB_UVC_ValidateConfig(&cfg),SYS_OK);return 0;}
 if(id==3){cfg.max_frame_bytes=70000;EQ(USB_UVC_ValidateConfig(&cfg),SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==4){EQ(Ov5640_ValidateConfig(&portrait),SYS_ERR_NOT_SUPPORTED);EQ(CameraCapture_ValidateStreamConfig(&portrait),SYS_ERR_NOT_SUPPORTED);return 0;}
 __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
 EQ(USB_Device_Init(),SYS_OK);EQ(USB_UVC_Configure(&cfg),SYS_OK);
 desc=USB_UVC_GetFSConfigDescriptor(&length);EQ(length,158);
 for(off=0;off<length;off+=desc[off]){
  EQ(desc[off]>=2,true);EQ(off+desc[off]<=length,true);
  if(desc[off]==30&&desc[off+1]==0x24&&desc[off+2]==7){
   EQ(desc[off+5]|((uint32_t)desc[off+6]<<8),320);EQ(desc[off+7]|((uint32_t)desc[off+8]<<8),240);
   EQ(UVC_Read32(desc+off+17),cfg.max_frame_bytes);EQ(UVC_Read32(desc+off+21),400000);EQ(UVC_Read32(desc+off+26),400000);found++;
  }
 }
 EQ(found,1);if(id==5)return 0;
 EQ(USB_UVC_Register(),SYS_OK);EQ(USB_Device_Start(),SYS_OK);HAL_PCD_ResetCallback(&s_pcd);Configured();
 EQ(Ep1_Commit(),0);EQ(UVC_Read32(s_uvc_active->commit+18),cfg.max_frame_bytes);EQ(UVC_Read32(s_uvc_active->commit+4),400000);
 if(id==6)return 0;
 EQ(Ep1_Open(),0);EQ(s_uvc_active->ep1_opened,true);EQ(s_pcd.IN_ep[1].maxpacket,1023);return 0;
}
