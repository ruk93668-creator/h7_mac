#include "../UvcStreaming/stream_test.c"
#include "jpeg_fixture.inc"
int Stage02_TestRun(uint32_t id)
{
 VideoStreamConfig_t cfg={{160,120,CAMERA_PIXEL_FORMAT_JPEG},25,38400};
 uint16_t size;uint8_t *desc;uint32_t i,off=0,packet=0,n;UsbUvcStreamStatus_t st;
 if(id==0){cfg.max_frame_bytes=0;EQ(USB_UVC_ValidateConfig(&cfg),SYS_ERR_INVALID_ARG);return 0;}
 if(id==1){cfg.max_frame_bytes=262140;EQ(USB_UVC_ValidateConfig(&cfg),SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==2){cfg.camera.width=320;EQ(USB_UVC_ValidateConfig(&cfg),SYS_ERR_NOT_SUPPORTED);return 0;}
 __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
 EQ(USB_Device_Init(),SYS_OK);EQ(USB_UVC_Configure(&cfg),SYS_OK);
 desc=USB_UVC_GetFSConfigDescriptor(&size);EQ(size,158);EQ(((uint32_t)desc&3),0);
 EQ(desc[2],158);EQ(desc[91],55);EQ(desc[101],11);EQ(desc[103],6);EQ(desc[114],7);
 EQ(UVC_Read32(desc+121),800);EQ(UVC_Read32(desc+125),7680000);EQ(desc[158],0);EQ(desc[159],0);
 EQ(UVC_Read32(desc+129),38400);EQ(UVC_Read32(desc+133),400000);
 for(i=0;i<size;i+=desc[i]){EQ(desc[i]>=2,true);EQ(i+desc[i]<=size,true);}EQ(i,size);
 EQ(USB_UVC_Register(),SYS_OK);EQ(USB_Device_Start(),SYS_OK);HAL_PCD_ResetCallback(&s_pcd);Configured();
 if(id==3){EQ(USB_UVC_SubmitFrame(mjpeg_fixture,sizeof(mjpeg_fixture)),SYS_ERR_INVALID_STATE);return 0;}
 EQ(Ep1_Commit(),0);EQ(Ep1_Open(),0);EQ(UVC_Read32(s_uvc_active->commit+18),38400);
 if(id==4){EQ(USB_UVC_SubmitFrame((const uint8_t *)0xFFFFFFF0U,1113),SYS_ERR_INVALID_ARG);EQ(USB_UVC_SubmitFrame(mjpeg_fixture,0),SYS_ERR_INVALID_ARG);EQ(USB_UVC_SubmitFrame(mjpeg_fixture,38401),SYS_ERR_INVALID_ARG);return 0;}
 if(id==5){EQ(USB_UVC_SubmitFrame(mjpeg_fixture,4),SYS_ERR_INVALID_ARG);return 0;}
 EQ(USB_UVC_SubmitFrame(mjpeg_fixture,sizeof(mjpeg_fixture)),SYS_OK);
 if(id==6){EQ(USB_UVC_SubmitFrame(mjpeg_fixture,sizeof(mjpeg_fixture)),SYS_ERR_BUSY);return 0;}
 Tick();USB_UVC_Process();EQ(s_uvc_active->stream_state,USB_UVC_IN_FLIGHT);
 if(id==7){HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);USB_UVC_Process();EQ(USB_UVC_GetStreamStatus(&st),SYS_OK);EQ(st.frame_owned,false);EQ(s_uvc_active->completed_frames,0);return 0;}
 if(id==8){fault=20;HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);USB_UVC_Process();EQ(USB_UVC_GetStreamStatus(&st),SYS_OK);EQ(st.frame_owned,true);fault=0;USB_UVC_Process();EQ(USB_UVC_GetStreamStatus(&st),SYS_OK);EQ(st.frame_owned,false);return 0;}
 if(id==9){Request(1,USB_REQ_SET_INTERFACE,0,1,0);EQ(s_uvc_payload.frame,NULL);return 0;}
 if(id==10){HAL_PCD_ResetCallback(&s_pcd);EQ(s_uvc_payload.frame,NULL);return 0;}
 if(id==11){HAL_PCD_DisconnectCallback(&s_pcd);EQ(s_uvc_payload.frame,NULL);return 0;}
 while(s_uvc_payload.frame!=NULL){
  if(s_uvc_active->stream_state!=USB_UVC_IN_FLIGHT){Tick();USB_UVC_Process();continue;}
  n=sizeof(mjpeg_fixture)-off;if(n>1021)n=1021;
  EQ(s_tx_transfer[1].total,n+2);EQ(s_tx_pio_pool[1][0],2);
  EQ((s_tx_pio_pool[1][1]&2)!=0,off+n==sizeof(mjpeg_fixture));
  EQ(memcmp(s_tx_pio_pool[1]+2,mjpeg_fixture+off,n),0);
  EQ(s_uvc_payload.frame_offset,off);Done();EQ(s_uvc_payload.frame_offset,off);
  USB_UVC_Process();off+=n;packet++;
 }
 EQ(packet,2);EQ(off,1113);EQ(s_uvc_active->completed_frames,1);EQ(s_uvc_payload.fid,1);
 EQ(USB_UVC_GetStreamStatus(&st),SYS_OK);EQ(st.frame_owned,false);
 if(id==12){EQ(USB_UVC_SubmitFrame(mjpeg_fixture,sizeof(mjpeg_fixture)),SYS_OK);while(s_uvc_payload.frame!=NULL){Tick();USB_UVC_Process();if(s_uvc_active->stream_state==USB_UVC_IN_FLIGHT){Done();USB_UVC_Process();}}EQ(s_uvc_payload.fid,0);EQ(s_uvc_active->completed_frames,2);}
 return 0;
}
