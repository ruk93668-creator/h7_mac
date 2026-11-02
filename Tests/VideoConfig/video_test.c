#include "../UsbCoreDecoupling/core_test.c"
#include "../../CM7/Service/usb_uvc_class.c"
#include "../../CM7/Application/app_uvc_bridge.c"

static VideoConfig_t test_config = {{160,120,CAMERA_PIXEL_FORMAT_YUV422},VIDEO_OUTPUT_USB_UVC,10};
const VideoConfig_t *VideoConfig_GetDefault(void) {return &test_config;}
static unsigned bridge_gets;
static uint8_t video_frame[38400];
bool CameraCapture_StreamGetFrame(CameraFrame_t *f){(void)f;bridge_gets++;return false;}
SysResult_t CameraCapture_StreamReleaseFrame(void){return SYS_OK;}
static uint32_t Read32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t Read16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}

int Video_TestRun(uint32_t id)
{
    VideoStreamConfig_t cfg={{160,120,CAMERA_PIXEL_FORMAT_YUV422},10};
    UvcStreamingControl_t control;
    const uint8_t *descriptor;
    uint16_t length,offset;
    uint32_t bytes;
    uint8_t data[34];
    unsigned index;
    if (id<5) {
        static const uint16_t rates[]={5,10,15,20,25};
        cfg.fps=rates[id];
        EQ(USB_UVC_Configure(&cfg),SYS_OK);
        descriptor=USB_UVC_GetFSConfigDescriptor(&length);EQ(length,180);EQ((uintptr_t)descriptor%4,0);
        for(offset=0;offset<length;offset+=descriptor[offset]) {
            EQ(descriptor[offset]>=2,true);EQ(offset+descriptor[offset]<=length,true);
            if(descriptor[offset]==30 && descriptor[offset+1]==0x24 && descriptor[offset+2]==5) {
                EQ(Read16(descriptor+offset+5),160);EQ(Read16(descriptor+offset+7),120);
                EQ(Read32(descriptor+offset+9),38400*8U*cfg.fps);
                EQ(Read32(descriptor+offset+13),38400*8U*cfg.fps);
                EQ(Read32(descriptor+offset+17),38400);
                EQ(Read32(descriptor+offset+21),10000000U/cfg.fps);
                EQ(Read32(descriptor+offset+26),10000000U/cfg.fps);
            }
        }
        UVC_DefaultControl(&control);EQ(control.dwFrameInterval,10000000U/cfg.fps);
        EQ(control.dwMaxVideoFrameSize,38400);EQ(control.dwMaxPayloadTransferSize,1023);
        EQ(UVC_ValidateControl(&control),true);
        UVC_EncodeControl(&control,data);EQ(Read32(data+4),10000000U/cfg.fps);
        EQ(USB_UVC_Configure(&cfg),SYS_ERR_INVALID_STATE);return 0;
    }
    if(id==5){EQ(USB_UVC_Configure(NULL),SYS_ERR_INVALID_ARG);return 0;}
    if(id==6){cfg.fps=0;EQ(USB_UVC_Configure(&cfg),SYS_ERR_INVALID_ARG);return 0;}
    if(id==7){cfg.fps=30;EQ(USB_UVC_Configure(&cfg),SYS_ERR_NOT_SUPPORTED);return 0;}
    if(id==8){cfg.camera.format=CAMERA_PIXEL_FORMAT_Y8;EQ(USB_UVC_Configure(&cfg),SYS_ERR_NOT_SUPPORTED);return 0;}
    if(id==9){cfg.camera.width=159;EQ(USB_UVC_Configure(&cfg),SYS_ERR_INVALID_ARG);return 0;}
    if(id==10){cfg.camera.height=0;EQ(USB_UVC_Configure(&cfg),SYS_ERR_INVALID_ARG);return 0;}
    if(id==11){EQ(USB_UVC_Register(),SYS_ERR_INVALID_STATE);return 0;}
    if(id==12){length=999;EQ(USB_UVC_GetFSConfigDescriptor(&length),NULL);EQ(length,0);return 0;}
    if(id==13){
        EQ(App_UvcBridgePrepareOutput(),SYS_OK);EQ(s_boot_mode,USB_MODE_UVC);
        EQ(s_bridge_config.width,160);EQ(s_bridge_frame_bytes,38400);
        EQ(s_video_frame_bytes,38400);EQ(s_video_interval,1000000);
        EQ(App_UvcBridgePrepareOutput(),SYS_ERR_INVALID_STATE);return 0;
    }
    if(id>=14 && id<=17){
        test_config.output=id==14?VIDEO_OUTPUT_NONE:id==15?VIDEO_OUTPUT_USB_TEST:id==16?VIDEO_OUTPUT_ETH:VIDEO_OUTPUT_WIFI;
        EQ(App_UvcBridgePrepareOutput(),id>=16?SYS_ERR_NOT_SUPPORTED:SYS_OK);
        if(id<16){EQ(s_video_configured,false);EQ(s_bridge_configured,false);App_UvcBridgeProcess();EQ(bridge_gets,0);}
        return 0;
    }
    if(id==18){
        EQ(App_UvcBridgePrepareOutput(),SYS_OK);
        __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
        EQ(USB_Device_Init(),SYS_OK);EQ(USB_Mode_Init(),SYS_OK);
        EQ(USB_Mode_Start(),SYS_OK);EQ(start_count,1);
        HAL_PCD_ResetCallback(&s_pcd);Configured();
        EQ(s_device.pUserData[0]!=NULL,true);
        EQ(USB_Mode_SelectBootMode(USB_MODE_TEST,NULL),SYS_ERR_INVALID_STATE);
        EQ(USB_UVC_Configure(&cfg),SYS_ERR_INVALID_STATE);return 0;
    }
    if(id==19){
        test_config.output=VIDEO_OUTPUT_NONE;EQ(App_UvcBridgePrepareOutput(),SYS_OK);
        EQ(USB_Mode_Init(),SYS_OK);EQ(USB_Mode_Start(),SYS_OK);EQ(start_count,0);return 0;
    }
    if(id==20){
        cfg.camera.width=128;cfg.camera.height=96;
        EQ(USB_UVC_Configure(&cfg),SYS_OK);UVC_DefaultControl(&control);
        EQ(control.dwMaxVideoFrameSize,24576);return 0;
    }
    if(id==21){
        cfg.camera.width=65534;cfg.camera.height=65535;
        EQ(CameraConfig_GetFrameSize(&cfg.camera,&bytes),SYS_ERR_INVALID_ARG);return 0;
    }
    if(id==22){
        EQ(USB_Mode_SelectBootMode(USB_MODE_UVC,NULL),SYS_ERR_INVALID_ARG);
        EQ(USB_Mode_SelectBootMode(USB_MODE_MSC,NULL),SYS_ERR_NOT_SUPPORTED);return 0;
    }
    if(id==23){
        cfg.camera.width=0;EQ(USB_UVC_Configure(&cfg),SYS_ERR_INVALID_ARG);
        cfg.camera.width=160;EQ(USB_UVC_Configure(&cfg),SYS_OK);return 0;
    }
    if(id==24){
        EQ(App_UvcBridgePrepareOutput(),SYS_OK);
        UVC_DefaultControl(&control);UVC_EncodeControl(&control,data);
        EQ(UVC_ProbeSet((UsbUvcContext_t *)&s_class_memory[0],data),true);
        return 0;
    }
    if(id>=25 && id<=27){
        UsbUvcContext_t *ctx;
        unsigned frame,packet,ticks;
        uint32_t previous_start=0,offset_before;
        static const uint16_t fps[]={5,10,20};
        test_config.output_fps=fps[id-25];
        EQ(App_UvcBridgePrepareOutput(),SYS_OK);
        __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
        EQ(USB_Device_Init(),SYS_OK);EQ(USB_Mode_Init(),SYS_OK);EQ(USB_Mode_Start(),SYS_OK);
        HAL_PCD_ResetCallback(&s_pcd);Configured();ctx=s_device.pUserData[0];
        UVC_DefaultControl(&control);UVC_EncodeControl(&control,data);
        EQ(UVC_ProbeSet(ctx,data),true);
        /* This transport fixture models an already acknowledged Probe readback. */
        ctx->probe_observed=true;EQ(UVC_CommitSet(ctx,data),true);
        EQ(UVC_SetStreamAlt(ctx,1),USBD_OK);
        for(frame=0;frame<2;frame++){
            EQ(USB_UVC_SubmitFrame(video_frame,sizeof(video_frame)),SYS_OK);
            for(packet=0;packet<38;packet++){
                ticks=0;
                while(ctx->stream_state!=USB_UVC_IN_FLIGHT && ticks++<2000){
                    s_pcd.FrameNumber++;HAL_PCD_SOFCallback(&s_pcd);USB_UVC_Process();
                }
                EQ(ctx->stream_state,USB_UVC_IN_FLIGHT);
                EQ(s_tx_transfer[1].total,packet==37?625:1023);
                EQ(s_tx_pio_pool[1][0],2);
                EQ(s_tx_pio_pool[1][1],0x80U|frame|(packet==37?2U:0U));
                EQ(s_uvc_payload.frame_offset,packet*1021U);
                if(packet==0){
                    if(frame)EQ(ctx->frame_sof-previous_start>=1000U/test_config.output_fps,true);
                    previous_start=ctx->frame_sof;
                }
                offset_before=s_uvc_payload.frame_offset;
                HAL_PCD_DataInStageCallback(&s_pcd,1);
                EQ(s_uvc_payload.frame_offset,offset_before);
                USB_UVC_Process();
            }
            EQ(ctx->completed_frames,frame+1);EQ(ctx->stream_state,USB_UVC_WAIT_FRAME);
            EQ(s_uvc_payload.frame,NULL);
        }
        return 0;
    }
    (void)index;return -1;
}
