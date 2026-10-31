//运行当前正式BSP/Core；仅IRQ调度使用真正的ST HAL函数
#include "../UsbCoreDecoupling/core_test.c"
void Race_HAL_PCD_IRQHandler(PCD_HandleTypeDef *pcd);
volatile uint32_t g_race[16];
volatile uint32_t g_race_phase;

static void Race_Irq(uint32_t in_flags, uint32_t out_flags)
{
    uint32_t USBx_BASE=(uint32_t)s_pcd.Instance;
    USBx_INEP(0)->DIEPINT=in_flags;
    USBx_OUTEP(0)->DOEPINT=out_flags;
    //另外保留EP1的真实完成事件，证明清理只针对EP0
    USBx_INEP(1)->DIEPINT=USB_OTG_DIEPINT_XFRC;
    USBx_DEVICE->DIEPMSK=USB_OTG_DIEPMSK_XFRCM|USB_OTG_DIEPMSK_EPDM;
    USBx_DEVICE->DOEPMSK=USB_OTG_DOEPMSK_XFRCM|USB_OTG_DOEPMSK_STUPM|USB_OTG_DOEPMSK_EPDM;
    USBx_DEVICE->DAINTMSK|=0x00050003U;
    USBx_DEVICE->DAINT=0x00010003U;
    s_pcd.Instance->GINTMSK=USB_OTG_GINTMSK_OEPINT|USB_OTG_GINTMSK_IEPINT;
    s_pcd.Instance->GINTSTS=USB_OTG_GINTSTS_OEPINT|USB_OTG_GINTSTS_IEPINT;
    *(__IO uint32_t *)(&s_pcd.Instance->CID+1)=USB_OTG_CORE_ID_310A;
    s_pcd.Instance->GRSTCTL=USB_OTG_GRSTCTL_AHBIDL;
    g_race_phase=1;
    Race_HAL_PCD_IRQHandler(&s_pcd);
    g_race_phase=2;
}

int Ep0Race_Run(uint32_t scenario)
{
    BspUsbEndpointConfig_t entries[2]={{0x81,1,512},{2,2,64}};
    uint8_t *setup=(uint8_t *)s_pcd.Setup;
    uint32_t USBx_BASE=(uint32_t)USB2_OTG_FS;
    uint32_t abort_before;
    UsbTxTransfer_t previous;
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
    g_result.case_id=scenario;
    Stage2_ConfigBuild(entries,2);EQ(Stage2_Boot(true),0);
    EQ(USB_Device_OpenEp(0x81,1,512),SYS_OK);EQ(USB_Device_Transmit(0x81,stage2_data,512),SYS_OK);
    EQ(USB_Device_OpenEp(2,2,64),SYS_OK);EQ(USB_Device_Receive(2,stage2_data+256,64),SYS_OK);
    Request(0x80,USB_REQ_GET_DESCRIPTOR,0x100,0,18);
    EQ(s_tx_transfer[0].active,true);EQ(s_endpoints[1][0].busy,true);
    //新请求改为Configuration，绝不先注入旧IN完成回调
    setup[0]=0x80;setup[1]=USB_REQ_GET_DESCRIPTOR;setup[2]=0;setup[3]=2;
    setup[4]=0;setup[5]=0;setup[6]=255;setup[7]=0;
    //补充：旧事务已进入STATUS OUT，取消时仍不得清共享RX
    if(scenario==3){HAL_PCD_DataInStageCallback(&s_pcd,0);EQ(s_endpoints[0][0].busy,true);}
    previous=s_tx_transfer[0];
    abort_before=abort_count;if(scenario==2)fault=20;
    if(scenario==1)USBx_INEP(0)->DIEPCTL|=USB_OTG_DIEPCTL_EPENA;
    Race_Irq((scenario==0||scenario==2)?USB_OTG_DIEPINT_XFRC:0,USB_OTG_DOEPINT_STUP);
    g_race[0]=s_tx_transfer[0].active;g_race[1]=s_tx_transfer[0].total;
    g_race[2]=s_tx_transfer[0].offset;g_race[3]=s_endpoints[1][0].busy;
    g_race[4]=s_device.ep0_state;g_race[5]=s_stop_required;
    g_race[6]=abort_count-abort_before;g_race[7]=stage2_in_calls;
    g_race[8]=USBx_DEVICE->DAINTMSK;g_race[9]=USBx_INEP(0)->DIEPINT;
    g_race[10]=flush_by_ep[0][0];g_race[11]=s_rx_active[2];
    EQ(abort_count-abort_before,1);EQ(stage2_in_calls,1);EQ(s_rx_active[2],true);
    EQ(s_pcd.OUT_ep[2].xfer_buff,stage2_data+256);EQ(flush_by_ep[0][0],0);
    if(scenario==2){
        EQ(s_stop_required,true);EQ(s_tx_transfer[0].active,true);
        EQ(s_tx_transfer[0].total,18);EQ(s_endpoints[1][0].busy,true);
        EQ(memcmp(&previous,&s_tx_transfer[0],sizeof(previous)),0);
        EQ(USBx_DEVICE->DAINTMSK&1U,0);EQ(USBx_INEP(0)->DIEPINT&USB_OTG_DIEPINT_XFRC,USB_OTG_DIEPINT_XFRC);return 0;
    }
    EQ(s_stop_required,false);EQ(s_tx_transfer[0].active,true);
    EQ(s_tx_transfer[0].total,stage2_config_length);EQ(s_tx_transfer[0].offset,stage2_config_length);
    EQ(s_tx_transfer[0].source,stage2_config);EQ(s_endpoints[1][0].busy,true);EQ(s_device.ep0_state,USBD_EP0_DATA_IN);
    //紧接着注入新事务真正的XFRC，不能丢失或“跳过下一次完成”
    g_race_phase=0;
    USBx_INEP(0)->DIEPINT=USB_OTG_DIEPINT_XFRC;
    USBx_INEP(1)->DIEPINT=0;USBx_OUTEP(0)->DOEPINT=0;
    USBx_DEVICE->DAINT=1;s_pcd.Instance->GINTSTS=USB_OTG_GINTSTS_IEPINT;
    g_race_phase=1;Race_HAL_PCD_IRQHandler(&s_pcd);g_race_phase=2;
    EQ(s_tx_transfer[0].active,false);EQ(s_endpoints[1][0].busy,false);
    EQ(s_device.ep0_state,USBD_EP0_STATUS_OUT);return 0;
}

//真实PCD初始化对象的链接桩，本测试不调用该初始化函数
uint32_t HAL_RCC_GetHCLKFreq(void){return 240000000U;}
