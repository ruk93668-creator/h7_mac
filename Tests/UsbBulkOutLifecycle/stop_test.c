//Real BSP + Platform + separately linked real HAL/LL. Only external Core/board boundaries are doubles.
#include "../../CM7/BSP/bsp_usb.c"
#define s_started s_platform_started
#define s_callbacks s_platform_callbacks
#include "../../CM7/Platform/usb_device.c"
#undef s_started
#undef s_callbacks
volatile uint32_t g_bulk_unexpected;
volatile uint32_t g_stop_observation[46];
static uint8_t s_data[64] __attribute__((aligned(32))),s_next[64],s_aux[64],s_control[64];
static uint32_t s_tick,s_out_completions,s_in_completions,s_setup_count,s_iso_count,s_nvic_changes,s_rearm;
static UsbBulkOutStopTransaction_t s_transaction;
void *memset(void *d,int c,size_t n) { volatile uint8_t *p=d;while(n--)*p++=(uint8_t)c;return d; }
void *memcpy(void *d,const void *s,size_t n) { volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;return d; }
void __aeabi_memclr(void *d,size_t n) { volatile uint8_t *p=d;while(n--)*p++=0U; }
void __aeabi_memclr4(void *d,size_t n) { volatile uint8_t *p=d;while(n--)*p++=0U; }
void __aeabi_memcpy(void *d,const void *s,size_t n) { volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++; }
void __aeabi_memcpy4(void *d,const void *s,size_t n) { volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++; }
void __aeabi_memset(void *d,size_t n,int c) { volatile uint8_t *p=d;while(n--)*p++=(uint8_t)c; }
uint32_t HAL_GetTick(void) { return s_tick; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 240000000U; }
HAL_StatusTypeDef HAL_PCDEx_ActivateLPM(PCD_HandleTypeDef *h) { (void)h;++g_bulk_unexpected;return HAL_ERROR; }
void HAL_PCDEx_LPM_Callback(PCD_HandleTypeDef *h,PCD_LPM_MsgTypeDef m) { (void)h;(void)m;++g_bulk_unexpected; }
void HAL_NVIC_EnableIRQ(IRQn_Type n) { ++s_nvic_changes;NVIC->ISER[(uint32_t)n>>5U]|=1UL<<((uint32_t)n&31U); }
void HAL_NVIC_DisableIRQ(IRQn_Type n) { ++s_nvic_changes;NVIC->ISER[(uint32_t)n>>5U]&=~(1UL<<((uint32_t)n&31U)); }
USBD_StatusTypeDef USBD_LL_DataOutStage(USBD_HandleTypeDef *d,uint8_t ep,uint8_t *data)
{ (void)d;(void)data;++s_out_completions;if(s_rearm && ep==2U) return USB_Device_Receive(ep,s_next,64U)==SYS_OK?USBD_OK:USBD_FAIL;return USBD_OK; }
USBD_StatusTypeDef USBD_LL_DataInStage(USBD_HandleTypeDef *d,uint8_t ep,uint8_t *data)
{ (void)d;(void)ep;(void)data;++s_in_completions;return USBD_OK; }
static void TestSetup(uint8_t *data) { (void)data;++s_setup_count; }
static void TestIso(uint8_t ep) { (void)ep;++s_iso_count; }
static const BspUsbCallbacks_t s_test_callbacks={TestSetup,Usb_DataOut,Usb_DataIn,Usb_Reset,NULL,NULL,NULL,Usb_Disconnect,NULL,TestIso,Usb_DataInError,NULL,Usb_OutLifecycleFault};
int Stop_Step(uint32_t op,uint32_t arg)
{
 SysResult_t result=SYS_OK; BspUsbOutStopProof_t proof;uint32_t USBx_BASE=(uint32_t)USB2_OTG_FS;
 if(op==0U)
 {
  memset(&s_pcd,0,sizeof(s_pcd));memset(s_data,0xAA,sizeof(s_data));memset(s_next,0xBB,sizeof(s_next));
  s_pcd.Instance=USB2_OTG_FS;s_pcd.Init.dev_endpoints=9U;s_pcd.Init.dma_enable=DISABLE;s_pcd.Init.phy_itface=PCD_PHY_EMBEDDED;s_pcd.Init.speed=PCD_SPEED_FULL;s_pcd.State=HAL_PCD_STATE_READY;
  s_pcd.OUT_ep[0].num=0U;s_pcd.OUT_ep[0].type=EP_TYPE_CTRL;s_pcd.OUT_ep[0].maxpacket=64U;
  s_ready=true;s_started=false;s_out_bind_window=arg!=2U;s_callbacks=&s_test_callbacks;s_bulk_controller=1U;s_fifo_valid=true;s_tx_fifo_words[1]=64U;
  s_initialized=true;s_platform_started=false;s_registered=true;s_class_state=USB_CLASS_INACTIVE;
  USBx_DEVICE->DCTL=USB_OTG_DCTL_SDIS;
  result=USB_Device_EnableBulkOutLifecycle();
  if(result==SYS_OK) result=BSP_USB_Start();
  s_platform_started=true;s_class_state=USB_CLASS_ACTIVE;s_class_memory_used=true;s_device.dev_state=USBD_STATE_CONFIGURED;
  s_endpoint_requirement_count=3U;
  s_endpoint_requirements[0].address=2U;s_endpoint_requirements[0].type=2U;s_endpoint_requirements[0].max_packet=64U;
  s_endpoint_requirements[1].address=1U;s_endpoint_requirements[1].type=arg==1U?1U:2U;s_endpoint_requirements[1].max_packet=64U;
  s_endpoint_requirements[2].address=0x81U;s_endpoint_requirements[2].type=2U;s_endpoint_requirements[2].max_packet=64U;
  s_pcd.Instance->GINTMSK=USB_OTG_GINTMSK_RXFLVLM|USB_OTG_GINTMSK_OEPINT|USB_OTG_GINTMSK_IEPINT|USB_OTG_GINTMSK_SOFM|USB_OTG_GINTMSK_USBRST|USB_OTG_GINTMSK_OTGINT;
  USBx_DEVICE->DOEPMSK=USB_OTG_DOEPMSK_XFRCM|USB_OTG_DOEPMSK_STUPM;
  USBx_DEVICE->DIEPMSK=USB_OTG_DIEPMSK_XFRCM;
  if(result==SYS_OK)result=USB_Device_OpenEp(2U,2U,64U);
  if(result==SYS_OK)result=USB_Device_OpenEp(1U,arg==1U?1U:2U,64U);
  if(result==SYS_OK)result=USB_Device_OpenEp(0x81U,2U,64U);
  s_endpoints[0][0].opened=true;USBx_DEVICE->DAINTMSK|=1UL<<16;s_nvic_changes=0U;
 }
 else if(op==1U) result=USB_Device_Receive(2U,s_data,64U);
 else if(op==2U) result=USB_Device_GetBulkOutTransfer(2U,&s_transaction);
 else if(op==3U) result=USB_Device_BeginBulkOutStop(&s_transaction,20U,&proof);
 else if(op==4U) result=USB_Device_PollBulkOutStop(&s_transaction,&proof);
 else if(op==5U) result=USB_Device_CommitBulkOutRetirement(&s_transaction,&proof);
 else if(op==6U) OTG_FS_IRQHandler();
 else if(op==7U) s_tick=arg;
 else if(op==8U) result=USB_Device_Receive(2U,s_next,64U);
 else if(op==10U) {UsbBulkOutStopTransaction_t old=s_transaction;++old.transfer.transfer_sequence;result=USB_Device_PollBulkOutStop(&old,&proof);}
 else if(op==11U) result=USB_Device_ReconcileBulkOutStop(&s_transaction,&proof);
 else if(op==12U) result=USB_Device_Receive(1U,s_aux,64U);
 else if(op==13U) s_rearm=1U;
 else if(op==14U) result=USB_Device_Transmit(0x81U,s_aux,4U);
 else if(op==16U) result=USB_Device_Stop();
 else if(op==17U) result=BSP_USB_Stop();
 else if(op==18U) result=USB_Device_CloseEp(2U);
 else if(op==19U) result=USB_Device_AbortEp(2U);
 else if(op==20U) result=USB_Device_FlushEp(2U);
 else if(op==21U) HAL_PCD_DataOutStageCallback(&s_pcd,2U);
 else if(op==23U) s_pcd.OUT_ep[2].xfer_buff=s_data+63U;
 else if(op==24U) result=USB_Device_Receive(2U,(uint8_t*)0x08000000U,64U);
 else if(op==25U) s_out_attempt_seed=UINT32_MAX;
 else if(op==26U) s_out_transfer_seed[2]=UINT32_MAX;
 else if(op==27U) s_out_lease_seed=UINT32_MAX;
 else if(op==28U) s_out_slot[2].polls=4095U;
 else if(op==29U) {s_endpoints[0][0].type=0U;result=(SysResult_t)USBD_LL_PrepareReceive(&s_device,0U,s_control,64U);}
 else if(op==30U) result=USB_Device_EnableBulkOutLifecycle();
 else if(op==31U) {s_out_event_seed=UINT32_MAX;}
 else if(op==32U) result=(SysResult_t)USBD_LL_PrepareReceive(&s_device,0U,NULL,0U);
 else if(op==33U) (void)USB_EP0_OutStart(s_pcd.Instance,0U,(uint8_t*)s_pcd.Setup);
 memset(&proof,0,sizeof(proof));(void)BSP_USB_GetBulkOutProof(2U,&proof);
 g_stop_observation[0]=result;g_stop_observation[1]=proof.stage;g_stop_observation[2]=proof.stop_attempt;g_stop_observation[3]=proof.transfer.transfer_sequence;
 g_stop_observation[4]=proof.buffer_owned;g_stop_observation[5]=proof.fenced;g_stop_observation[6]=proof.received;g_stop_observation[7]=proof.pio_references;
 g_stop_observation[8]=proof.events;g_stop_observation[9]=proof.source_returns;g_stop_observation[10]=proof.lease_id;g_stop_observation[11]=proof.barrier_sequence;
 g_stop_observation[12]=proof.disable_requested;g_stop_observation[13]=proof.disable_observed;g_stop_observation[14]=proof.events_retired;g_stop_observation[15]=proof.normal_complete;
 g_stop_observation[16]=proof.lease_done;g_stop_observation[17]=proof.error;g_stop_observation[18]=proof.first_error;g_stop_observation[19]=proof.reconcile_count;
 g_stop_observation[20]=s_out_lease.id;g_stop_observation[21]=s_out_lease.bulk_mask;g_stop_observation[22]=s_out_lease.iso_mask;g_stop_observation[23]=s_out_lease.fault;
 g_stop_observation[24]=s_out_completions;g_stop_observation[25]=s_in_completions;g_stop_observation[26]=s_setup_count;g_stop_observation[27]=s_iso_count;
 g_stop_observation[28]=s_endpoints[0][2].busy;g_stop_observation[29]=s_nvic_changes;g_stop_observation[30]=s_class_memory_used;g_stop_observation[31]=s_out_session;
 g_stop_observation[32]=proof.transfer.session_generation;g_stop_observation[33]=*(uint32_t*)s_data;g_stop_observation[34]=*(uint32_t*)s_next;g_stop_observation[35]=*(uint32_t*)s_aux;
 g_stop_observation[36]=s_pcd.Setup[0];g_stop_observation[37]=s_pcd.Setup[1];g_stop_observation[38]=g_bulk_unexpected;g_stop_observation[39]=s_out_lease.releasing;
 g_stop_observation[40]=s_out_slot[1].proof.buffer_owned;g_stop_observation[41]=s_out_slot[1].proof.fenced;
 g_stop_observation[42]=s_out_slot[1].proof.source_returns;g_stop_observation[43]=s_out_slot[0].proof.buffer_owned;
 g_stop_observation[44]=NVIC_GetEnableIRQ(OTG_FS_IRQn);g_stop_observation[45]=s_endpoints[0][0].busy;
 return 0;
}
