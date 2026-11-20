//Witness existing HAL/LL behavior. These checks confirm gates, not a workaround.
#include "../UsbBulkOut/hal_out_evidence.c"
volatile uint32_t g_gate[4];
int Gate_Step(uint32_t op,uint32_t arg)
{
 HAL_StatusTypeDef r=HAL_OK;uint32_t USBx_BASE=(uint32_t)USB2_OTG_FS;(void)arg;
 if(op==0U){C0_Step(0U,0U);g_pcd.IN_ep[0].num=0U;g_pcd.IN_ep[0].maxpacket=64U;g_pcd.IN_ep[0].type=EP_TYPE_CTRL;g_pcd.State=HAL_PCD_STATE_READY;}
 else if(op==1U)r=HAL_PCD_EP_Transmit(&g_pcd,0x80U,NULL,0U);
 else if(op==2U)r=HAL_PCD_EP_Transmit(&g_pcd,0x81U,NULL,0U);
 else if(op==3U)r=HAL_PCD_EP_Abort(&g_pcd,0U);
 else if(op==4U)r=HAL_PCD_EP_Transmit(&g_pcd,0x81U,g_buffers[1],4U);
 g_gate[0]=r;g_gate[1]=(uint32_t)g_pcd.OUT_ep[0].xfer_buff;
 g_gate[2]=USBx_OUTEP(0U)->DOEPCTL;g_gate[3]=USBx_INEP(0U)->DIEPCTL;return 0;
}
