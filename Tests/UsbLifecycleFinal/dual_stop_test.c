//Actual HAL/LL + BSP/Platform; no proof fields are written by this fixture.
#define Stop_Step C1_PreviousStep
#include "../UsbBulkOutLifecycle/stop_test.c"
#undef Stop_Step
static uint32_t denied_deinit_calls;
static uint8_t DeniedDeInit(USBD_HandleTypeDef *d,uint8_t c){(void)d;(void)c;++denied_deinit_calls;return USBD_FAIL;}
volatile uint32_t g_final[18];
int Final_Step(uint32_t op,uint32_t arg)
{
 SysResult_t r=SYS_OK;UsbStopProgress_t p;
 if(op==0U){C1_PreviousStep(0U,0U);r=(SysResult_t)g_stop_observation[0];}
 else if(op==1U)r=USB_Device_Receive(2U,s_data,64U);
 else if(op==2U)r=USB_Device_Transmit(0x81U,s_aux,4U);
 else if(op==3U)r=USB_Device_Stop();
 else if(op==4U)OTG_FS_IRQHandler();
 else if(op==5U)r=USB_Device_Receive(1U,s_aux,64U);
 else if(op==6U)r=USB_Device_Transmit(0x81U,s_aux,4U);
 else if(op==7U)s_tick=arg;
 else if(op==8U){s_control_busy=true;r=USB_Device_Stop();s_control_busy=false;}
 else if(op==9U)s_stop_progress_seed=UINT32_MAX;
 else if(op==10U){C1_PreviousStep(2U,0U);C1_PreviousStep(3U,0U);}
 else if(op==11U){s_class_callbacks.DeInit=DeniedDeInit;r=(SysResult_t)Usb_ManagedClassDeInit(&s_device,1U);}
 else if(op==12U)Usb_Reset();
 else if(op==13U)Usb_Disconnect();
 else if(op==14U)r=(USB_Device_AbortEp(2U)==SYS_ERR_BUSY && USB_Device_CloseEp(2U)==SYS_ERR_BUSY && USB_Device_FlushEp(2U)==SYS_ERR_BUSY)?SYS_OK:SYS_ERR_IO;
 else if(op==15U)s_pcd.OUT_ep[0].maxpacket=0U; //adversarial cached EP0 state, not a retirement proof
 else if(op==16U){s_stop_progress.state=USB_STOP_PROGRESS_FAILED;s_stop_progress.transaction_id=0U;} //initial stale metadata, never a hardware proof
 (void)USB_Device_GetStopProgress(&p);
 g_final[0]=r;g_final[1]=p.transaction_id;g_final[2]=p.state;g_final[3]=p.endpoint_count;
 g_final[4]=p.pending_count;g_final[5]=p.registered_pending;g_final[6]=p.endpoint_group_proven;
 g_final[7]=p.controller_gate_blocked;g_final[8]=p.first_error;g_final[9]=s_out_slot[2].proof.buffer_owned;
 g_final[10]=s_bulk_in[1].proof.source_owned;g_final[11]=s_class_memory_used;g_final[12]=s_platform_started;
 g_final[13]=NVIC_GetEnableIRQ(OTG_FS_IRQn);g_final[14]=p.endpoints[1].attempt;g_final[15]=p.endpoints[8].attempt;
 g_final[16]=denied_deinit_calls+g_bulk_unexpected;g_final[17]=s_out_slot[1].proof.buffer_owned;
 return 0;
}
