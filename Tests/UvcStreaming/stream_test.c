//复用已有真实Core/Platform/BSP/Probe/Commit及HAL故障桩
#include "../UvcEp1Lifecycle/ep1_test.c"
static uint8_t s_frame[38400];
const uint32_t g_uvc_context_size=sizeof(UsbUvcContext_t);
volatile uint32_t g_in_callback;
volatile uint32_t g_hw_raise;
void Stream_HAL_PCD_IRQHandler(PCD_HandleTypeDef *pcd);
volatile uint32_t g_lengths[256], g_headers[256], g_sofs[256], g_packet_count;

//SOF中断入口不允许构包或发送
static void Tick(void)
{
    s_pcd.FrameNumber++;
    g_in_callback=1U; HAL_PCD_SOFCallback(&s_pcd); g_in_callback=0U;
}
static void Done(void)
{
    g_in_callback=1U; HAL_PCD_DataInStageCallback(&s_pcd,1U); g_in_callback=0U;
}
static int Start(void)
{
    EQ(USB_UVC_SubmitFrame(s_frame,sizeof(s_frame)),SYS_OK); return 0;
}
static int Send(void)
{
    Tick(); USB_UVC_Process(); EQ(s_uvc_active->stream_state,USB_UVC_IN_FLIGHT); return 0;
}
static int Frame(void)
{
    uint32_t i,j,offset=0U,before,expected;
    uint8_t fid=s_uvc_payload.fid;
    EQ(Start(),0U);
    for(i=0U;i<76U;i++)
    {
        while(s_uvc_active->stream_state!=USB_UVC_IN_FLIGHT) { Tick(); USB_UVC_Process(); }
        expected=i==75U?152U:512U;
        EQ(s_tx_transfer[1].total,expected); EQ(s_tx_pio_pool[1][0],2U);
        EQ(s_tx_pio_pool[1][1],0x80U|fid|(i==75U?2U:0U));
        for(j=0U;j<expected-2U;j++) EQ(s_tx_pio_pool[1][j+2U],s_frame[offset+j]);
        g_lengths[g_packet_count]=expected;g_headers[g_packet_count]=s_tx_pio_pool[1][1];g_sofs[g_packet_count++]=USB_Device_GetSofSequence();
        before=s_uvc_payload.frame_offset; EQ(before,offset); Done();
        EQ(s_uvc_payload.frame_offset,before); EQ(s_uvc_payload.fid,fid);
        USB_UVC_Process(); offset+=expected-2U;
    }
    EQ(offset,38400U);EQ(s_uvc_active->stream_state,USB_UVC_WAIT_FRAME);EQ(s_uvc_payload.fid,fid^1U);return 0;
}

int UvcStreaming_TestRun(uint32_t id)
{
    UsbUvcContext_t *ctx;
    UsbUvcStreamStatus_t status;
    uint32_t i,token,offset,seq;
    PCD_HandleTypeDef foreign={0};
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
    if(id==0U)
    {
        EQ(Mode_Boot(true),0U);Tick();USB_UVC_Process();EQ(USB_Device_IsConfigured(),true);
        EQ(s_pcd.Init.Sof_enable,ENABLE);EQ(s_uvc_active,NULL);return 0;
    }
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(),SYS_OK);EQ(USB_UVC_Register(),SYS_OK);EQ(USB_Device_Start(),SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);Configured();ctx=s_device.pUserData[0];
    if(id==20U)
    {
        EQ(USB_UVC_SubmitFrame(s_frame,sizeof(s_frame)),SYS_ERR_INVALID_STATE);
        EQ(Ep1_Commit(),0U);EQ(USB_UVC_SubmitFrame(s_frame,sizeof(s_frame)),SYS_ERR_INVALID_STATE);return 0;
    }
    EQ(Ep1_Commit(),0U);EQ(Ep1_Open(),0U);
    for(i=0U;i<sizeof(s_frame);i++)s_frame[i]=(uint8_t)(i*13U+(i>>8));
    if(id==1U||id==2U||id==21U||id==22U)
    {
        if(id==22U)
        {
            EQ(Start(),0U);
            for(i=0U;i<75U;i++){EQ(Send(),0U);Done();USB_UVC_Process();}
            EQ(Send(),0U);EQ(s_tx_transfer[1].total,152U);EQ(s_uvc_payload.fid,0U);EQ(ctx->completed_frames,0U);
            Done();EQ(s_uvc_payload.fid,0U);USB_UVC_Process();EQ(s_uvc_payload.fid,1U);EQ(ctx->completed_frames,1U);
        }
        else {EQ(Frame(),0U);EQ(g_packet_count,76U);if(id==2U||id==21U){EQ(Frame(),0U);EQ(g_packet_count,152U);EQ(g_sofs[76]-g_sofs[0],100U);}}
        return 0;
    }
    if(id==19U){seq=USB_Device_GetSofSequence();HAL_PCD_SOFCallback(&foreign);EQ(USB_Device_GetSofSequence(),seq);return 0;}
    if(id==28U)
    {
        EQ(Start(),0U);EQ(Send(),0U);Done();USB_UVC_Process();EQ(Send(),0U);
        offset=s_uvc_payload.frame_offset;UVC_DataIn(&s_device,1U);
        EQ(ctx->stream_state,USB_UVC_IN_FLIGHT);EQ(s_uvc_payload.frame_offset,offset);return 0;
    }
    if(id==29U)
    {
        EQ(USB_UVC_SubmitFrame(NULL,38400U),SYS_ERR_INVALID_ARG);
        EQ(USB_UVC_SubmitFrame(s_frame,38401U),SYS_ERR_INVALID_ARG);
        EQ(USB_UVC_GetStreamStatus(NULL),SYS_ERR_INVALID_ARG);return 0;
    }
    if(id==30U)
    {
        EQ(Start(),0U);s_in_sequence[1]=UINT32_MAX;Tick();USB_UVC_Process();
        EQ(ctx->stream_state,USB_UVC_RECOVERY);EQ(s_tx_transfer[1].active,false);EQ(s_uvc_payload.frame_offset,0U);return 0;
    }
    if(id==27U){EQ(sizeof(UsbUvcContext_t)<=1024U,true);EQ(s_class_memory_used,true);return 0;}
    if(id==33U) { for(i=0U;i<100U;i++){Tick();USB_UVC_Process();}EQ(ctx->stream_state,USB_UVC_WAIT_FRAME);EQ(s_tx_transfer[1].active,false);return 0; }
    EQ(Start(),0U);
    if(id==31U)
    {
        uint32_t USBx_BASE=(uint32_t)s_pcd.Instance;
        g_hw_raise=1U;
        USBx_DEVICE->DSTS=(1U<<USB_OTG_DSTS_FNSOF_Pos);
        s_pcd.Instance->GINTMSK=USB_OTG_GINTMSK_SOFM;
        s_pcd.Instance->GINTSTS=USB_OTG_GINTSTS_SOF;
        g_hw_raise=0U;g_in_callback=1U;
        Stream_HAL_PCD_IRQHandler(&s_pcd);g_in_callback=0U;
        EQ(USB_Device_GetSofSequence(),1U);EQ(s_tx_transfer[1].active,false);
        USB_UVC_Process();EQ(ctx->stream_state,USB_UVC_IN_FLIGHT);return 0;
    }
    if(id==3U){for(i=0U;i<20U;i++)USB_UVC_Process();EQ(s_tx_transfer[1].active,false);EQ(s_uvc_payload.frame_offset,0U);return 0;}
    if(id==6U||id==7U)
    {
        fault=id==6U?9U:21U;Tick();USB_UVC_Process();EQ(s_uvc_payload.frame_offset,0U);
        token=ctx->packet.token;EQ(ctx->stream_state,id==6U?USB_UVC_WAIT_SLOT:USB_UVC_RECOVERY);
        fault=0U;
        if(id==6U){USB_UVC_Process();EQ(s_tx_transfer[1].active,false);EQ(Send(),0U);EQ(ctx->packet.token,token);}
        else{USB_UVC_Process();EQ(s_uvc_payload.frame,NULL);EQ(ctx->completed_frames,0U);}
        return 0;
    }
    if(id==26U)
    {
        EQ(Ep1_MakeBusy(),0U);Tick();USB_UVC_Process();EQ(ctx->stream_state,USB_UVC_WAIT_SLOT);
        Done();EQ(s_uvc_payload.frame_offset,0U);EQ(Send(),0U);return 0;
    }
    EQ(Send(),0U);token=ctx->packet.token;offset=s_uvc_payload.frame_offset;
    if(id==34U)
    {
        fault=14U;Request(1U,USB_REQ_SET_INTERFACE,0U,1U,0U);EQ(Ep1_Stalled(),0U);
        EQ(ctx->ep1_opened,true);EQ(ctx->ep1_close_pending,true);EQ(s_uvc_payload.frame,s_frame);
        EQ(s_class_memory_used,true);Done();EQ(s_uvc_payload.frame,s_frame);
        fault=0U;Request(1U,USB_REQ_SET_INTERFACE,0U,1U,0U);EQ(Ep1_Status(),0U);
        EQ(s_uvc_payload.frame,NULL);EQ(ctx->ep1_opened,false);return 0;
    }
    if(id==35U)
    {
        fault=14U;HAL_PCD_ResetCallback(&s_pcd);EQ(s_device.pUserData[0],ctx);
        EQ(s_class_memory_used,true);EQ(s_uvc_payload.frame,s_frame);EQ(s_stop_required,true);
        fault=0U;EQ(USB_Device_Stop(),SYS_OK);EQ(s_uvc_payload.frame,NULL);EQ(s_class_memory_used,false);return 0;
    }
    if(id==32U)
    {
        uint32_t USBx_BASE=(uint32_t)s_pcd.Instance;
        g_hw_raise=1U;USBx_INEP(1)->DIEPINT=0x8BU;USBx_INEP(2)->DIEPINT=1U;g_hw_raise=0U;
        EQ(USB_UVC_CancelFrame(),SYS_OK);fault=20U;USB_UVC_Process();
        EQ(USBx_INEP(1)->DIEPINT,0x8BU);EQ(s_uvc_payload.frame,s_frame);
        fault=0U;USB_UVC_Process();EQ(USBx_INEP(1)->DIEPINT,0x80U);EQ(USBx_INEP(2)->DIEPINT,1U);
        Done();EQ(ctx->completed_frames,0U);EQ(s_uvc_payload.frame,NULL);return 0;
    }
    if(id==4U)
    {
        seq=USB_Device_GetSofSequence();HAL_PCD_SOFCallback(&s_pcd);EQ(USB_Device_GetSofSequence(),seq);
        Done();USB_UVC_Process();USB_UVC_Process();EQ(s_tx_transfer[1].active,false);EQ(s_uvc_payload.frame_offset,510U);
    }
    else if(id==5U){for(i=0U;i<10U;i++)Tick();Done();USB_UVC_Process();EQ(ctx->stream_state,USB_UVC_IN_FLIGHT);Done();USB_UVC_Process();EQ(ctx->stream_state,USB_UVC_WAIT_SLOT);}
    else if(id==8U){Done();Done();EQ(s_uvc_payload.frame_offset,offset);USB_UVC_Process();EQ(s_uvc_payload.frame_offset,510U);USB_UVC_Process();EQ(s_uvc_payload.frame_offset,510U);}
    else if(id==9U){Done();ctx->event_token=token+1U;USB_UVC_Process();EQ(ctx->stream_state,USB_UVC_RECOVERY);EQ(s_uvc_payload.frame_offset,0U);}
    else if(id==10U||id==11U||id==24U||id==25U)
    {
        if(id==25U)Done();
        HAL_PCD_ISOINIncompleteCallback(&s_pcd,1U);
        EQ(ctx->stream_state,USB_UVC_RECOVERY);EQ(s_uvc_payload.frame,s_frame);EQ(ctx->completed_frames,0U);
        if(id==24U)Done();
        if(id==11U)fault=20U;
        USB_UVC_Process();
        if(id==11U){EQ(s_uvc_payload.frame,s_frame);EQ(s_uvc_payload.pending_token,token);EQ(ctx->stream_state,USB_UVC_RECOVERY);fault=0U;USB_UVC_Process();}
        EQ(s_uvc_payload.frame,NULL);EQ(ctx->stream_state,USB_UVC_WAIT_FRAME);EQ(ctx->completed_frames,0U);
    }
    else if(id==12U||id==13U)
    {
        if(id==13U)fault=20U;
        Request(1U,USB_REQ_SET_INTERFACE,0U,1U,0U);
        if(id==13U){EQ(Ep1_Stalled(),0U);EQ(ctx->ep1_opened,true);EQ(s_uvc_payload.frame,s_frame);fault=0U;Request(1U,USB_REQ_SET_INTERFACE,0U,1U,0U);}
        EQ(Ep1_Status(),0U);EQ(s_uvc_payload.frame,NULL);EQ(ctx->ep1_opened,false);Tick();USB_UVC_Process();EQ(s_tx_transfer[1].active,false);
    }
    else if(id==14U||id==15U)
    {
        if(id==14U)HAL_PCD_ResetCallback(&s_pcd);else HAL_PCD_DisconnectCallback(&s_pcd);
        EQ(s_device.pUserData[0],NULL);EQ(s_uvc_payload.frame,NULL);Done();
        if(id==14U){Configured();EQ(Ep1_Commit(),0U);EQ(Ep1_Open(),0U);EQ(Start(),0U);EQ(Send(),0U);ctx=s_device.pUserData[0];EQ(ctx->packet.token>token,true);ctx->event_token=token;ctx->stream_state=USB_UVC_WAIT_CONFIRM;USB_UVC_Process();EQ(ctx->stream_state,USB_UVC_RECOVERY);EQ(s_uvc_payload.frame_offset,0U);}
    }
    else if(id==16U)
    {
        fault=20U;EQ(USBD_ClrClassConfig(&s_device,1U),USBD_FAIL);EQ(s_class_memory_used,true);EQ(s_device.pUserData[0],ctx);EQ(s_uvc_payload.frame,s_frame);
        fault=0U;EQ(USB_Device_Stop(),SYS_OK);EQ(s_uvc_payload.frame,NULL);EQ(s_class_memory_used,false);
    }
    else if(id==17U||id==18U||id==23U)
    {
        EQ(USB_UVC_CancelFrame(),SYS_OK);EQ(s_uvc_payload.frame,s_frame);
        if(id==18U)Done();USB_UVC_Process();EQ(s_uvc_payload.frame,NULL);EQ(ctx->completed_frames,0U);
        if(id==23U){Done();EQ(ctx->stream_state,USB_UVC_WAIT_FRAME);EQ(ctx->completed_frames,0U);}
    }
    else EQ(0U,1U);
    if(s_uvc_active){EQ(USB_UVC_GetStreamStatus(&status),SYS_OK);EQ(status.completed_frames,0U);}
    EQ(__get_PRIMASK(),0U);return 0;
}

//真实HAL IRQ的唤醒延时分支链接依赖，本测试不触发该分支
uint32_t HAL_RCC_GetHCLKFreq(void) { return 240000000U; }
