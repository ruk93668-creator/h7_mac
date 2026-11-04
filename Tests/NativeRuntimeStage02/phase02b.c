#include "../UvcStreaming/stream_test.c"
static uint32_t notify_count,notify_bits;
static void Wake(uint32_t e,void *p){(void)p;notify_count++;notify_bits|=e;}
int Stage02_TestRun(uint32_t id)
{
    UsbUvcStreamStatus_t st;
    VideoStreamConfig_t cfg={{160,120,CAMERA_PIXEL_FORMAT_YUV422},25};
    uint32_t n,previous,packets=0,first_sof=0;
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
    EQ(USB_Device_SetEventCallback(Wake,NULL),SYS_OK);
    EQ(USB_Device_Init(),SYS_OK);
    if(id==0){EQ(Usb_TestRegister(),SYS_OK);EQ(USB_Device_Start(),SYS_OK);Tick();EQ(notify_count>0,true);EQ(s_uvc_active,NULL);return 0;}
    EQ(USB_UVC_Configure(&cfg),SYS_OK);EQ(USB_UVC_Register(),SYS_OK);EQ(USB_Device_Start(),SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);Configured();EQ(Ep1_Commit(),0);EQ(Ep1_Open(),0);
    if(id==1){previous=notify_count;Tick();EQ(notify_count,previous+1);EQ(s_tx_transfer[1].active,false);return 0;}
    EQ(Start(),0);EQ(Send(),0);previous=notify_count;
    if(id==2){Done();EQ(notify_count,previous+1);EQ(s_uvc_payload.frame_offset,0);USB_UVC_Process();EQ(s_uvc_payload.frame_offset,UVC_VIDEO_PACKET_BYTES-2);return 0;}
    if(id==3 || id==4){fault=id==4?20:0;HAL_PCD_ISOINIncompleteCallback(&s_pcd,1);EQ(notify_count,previous+1);USB_UVC_Process();EQ(USB_UVC_GetStreamStatus(&st),SYS_OK);EQ(st.frame_owned,id==4);if(id==4){fault=0;USB_UVC_Process();EQ(USB_UVC_GetStreamStatus(&st),SYS_OK);EQ(st.frame_owned,false);}return 0;}
    if(id==5){Request(1,USB_REQ_SET_INTERFACE,0,1,0);EQ(notify_count>previous,true);EQ(s_uvc_payload.frame,NULL);return 0;}
    if(id==6){HAL_PCD_ResetCallback(&s_pcd);EQ(notify_count>previous,true);EQ(s_uvc_payload.frame,NULL);return 0;}
    if(id==7){HAL_PCD_DisconnectCallback(&s_pcd);EQ(notify_count>previous,true);EQ(s_uvc_payload.frame,NULL);return 0;}
    if(id==8){Tick();USB_UVC_Process();EQ(s_uvc_payload.frame_offset,0);Done();USB_UVC_Process();n=s_uvc_payload.frame_offset;UVC_DataIn(&s_device,1);USB_UVC_Process();EQ(s_uvc_payload.frame_offset,n);return 0;}
    /* Complete two configured 25 FPS frames, driven only by SOF/completion events. */
    while(s_uvc_payload.frame!=NULL){
        if(s_uvc_active->stream_state==USB_UVC_IN_FLIGHT){packets++;Done();USB_UVC_Process();}
        else {Tick();USB_UVC_Process();}
    }
    EQ(packets,(38400U+UVC_VIDEO_PACKET_BYTES-3U)/(UVC_VIDEO_PACKET_BYTES-2U));
    if(id==9){EQ(s_uvc_active->completed_frames,1);return 0;}
    first_sof=s_uvc_active->frame_sof;EQ(Start(),0);
    while(s_uvc_active->stream_state!=USB_UVC_IN_FLIGHT){Tick();USB_UVC_Process();}
    EQ(s_uvc_active->frame_sof-first_sof,40);return 0;
}
