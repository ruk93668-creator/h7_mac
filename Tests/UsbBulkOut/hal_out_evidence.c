// C0 evidence only: link the unchanged HAL/LL, never claim an OUT Stop implementation.
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stddef.h>
PCD_HandleTypeDef g_pcd;
uint8_t g_buffers[9][64], g_rearm[64];
volatile uint32_t g_c0[24];
static uint32_t out_callbacks, in_callbacks, setup_callbacks, iso_callbacks, reset_callbacks;
static uint32_t rearm_on_complete, last_read;
void *memset(void *d, int c, size_t n) { volatile uint8_t *p=d; while(n--) *p++=(uint8_t)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { volatile uint8_t *p=d; const volatile uint8_t *q=s; while(n--) *p++=*q++; return d; }
void __aeabi_memclr(void *d, size_t n) { volatile uint8_t *p=d; while(n--) *p++=0U; }
void __aeabi_memclr4(void *d, size_t n) { volatile uint8_t *p=d; while(n--) *p++=0U; }
void __aeabi_memcpy(void *d,const void *s,size_t n) { volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--) *p++=*q++; }
void __aeabi_memcpy4(void *d,const void *s,size_t n) { volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--) *p++=*q++; }
void __aeabi_memset(void *d,size_t n,int c) { volatile uint8_t *p=d; while(n--) *p++=(uint8_t)c; }
HAL_StatusTypeDef HAL_PCDEx_ActivateLPM(PCD_HandleTypeDef *h) { (void)h; ++g_c0[23]; return HAL_ERROR; }
void HAL_PCDEx_LPM_Callback(PCD_HandleTypeDef *h,PCD_LPM_MsgTypeDef msg) { (void)h;(void)msg;++g_c0[23]; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 240000000U; }
uint32_t HAL_GetTick(void) { return 0U; }
void HAL_Delay(uint32_t ms) { (void)ms; }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *h,uint8_t ep)
{ uint32_t USBx_BASE=(uint32_t)h->Instance; g_c0[22]=USBx_OUTEP(ep)->DOEPINT & USB_OTG_DOEPINT_XFRC; ++out_callbacks; if(rearm_on_complete && ep==2U) (void)HAL_PCD_EP_Receive(h,2U,g_rearm,64U); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *h,uint8_t ep) { (void)h;(void)ep;++in_callbacks; }
void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *h) { (void)h;++setup_callbacks; }
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *h,uint8_t ep) { (void)h;(void)ep;++iso_callbacks; }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *h) { (void)h;++reset_callbacks; }
int C0_Step(uint32_t op,uint32_t arg)
{
 uint32_t ep; uint32_t USBx_BASE=(uint32_t)USB2_OTG_FS;
 if(op==0U)
 {
  memset(&g_pcd,0,sizeof(g_pcd));memset(g_buffers,0xAA,sizeof(g_buffers));memset(g_rearm,0xBB,sizeof(g_rearm));
  g_pcd.Instance=USB2_OTG_FS;g_pcd.Init.dev_endpoints=9U;g_pcd.Init.dma_enable=DISABLE;
  g_pcd.Init.phy_itface=PCD_PHY_EMBEDDED;
  for(ep=0U;ep<9U;++ep) { g_pcd.OUT_ep[ep].num=ep;g_pcd.OUT_ep[ep].type=EP_TYPE_BULK;g_pcd.OUT_ep[ep].maxpacket=64U;g_pcd.OUT_ep[ep].xfer_buff=g_buffers[ep];g_pcd.OUT_ep[ep].xfer_len=64U; }
  g_pcd.OUT_ep[0].type=EP_TYPE_CTRL;
  g_pcd.IN_ep[1].num=1U;g_pcd.IN_ep[1].type=EP_TYPE_BULK;g_pcd.IN_ep[1].maxpacket=64U;g_pcd.IN_ep[1].xfer_len=4U;
 }
 else if(op==1U) HAL_PCD_IRQHandler(&g_pcd);
 else if(op==2U) last_read=g_pcd.Instance->GRXSTSR;
 else if(op==3U) last_read=g_pcd.Instance->GRXSTSP;
 else if(op==4U) (void)HAL_PCD_EP_Receive(&g_pcd,2U,g_rearm,64U);
 else if(op==5U) rearm_on_complete=arg;
 else if(op==6U) {g_pcd.OUT_ep[1].type=EP_TYPE_ISOC;USBx_OUTEP(1)->DOEPCTL=USB_OTG_DOEPCTL_EPENA;}
 else if(op==7U) g_pcd.OUT_ep[1].is_iso_incomplete=1U;
 else if(op==8U) last_read=HAL_PCD_EP_Abort(&g_pcd,2U);
 else if(op==9U) USBx_DEVICE->DCTL|=USB_OTG_DCTL_SGONAK;
 g_c0[0]=out_callbacks;g_c0[1]=in_callbacks;g_c0[2]=setup_callbacks;g_c0[3]=iso_callbacks;g_c0[4]=reset_callbacks;
 g_c0[5]=g_pcd.OUT_ep[2].xfer_count;g_c0[6]=(uint32_t)g_pcd.OUT_ep[2].xfer_buff;
 g_c0[7]=*(uint32_t*)g_buffers[2];g_c0[8]=*(uint32_t*)g_rearm;
 g_c0[9]=USBx_DEVICE->DCTL;g_c0[10]=g_pcd.Instance->GINTMSK;g_c0[11]=g_pcd.Instance->GINTSTS;
 g_c0[12]=USBx_OUTEP(2)->DOEPCTL;g_c0[13]=USBx_OUTEP(2)->DOEPINT;g_c0[14]=last_read;
 g_c0[15]=*(uint32_t*)g_buffers[1];g_c0[16]=g_pcd.OUT_ep[1].xfer_count;
 g_c0[17]=g_pcd.Setup[0];g_c0[18]=g_pcd.Setup[1];g_c0[19]=g_pcd.OUT_ep[1].is_iso_incomplete;
 g_c0[20]=USBx_OUTEP(1)->DOEPCTL;g_c0[21]=*(uint32_t*)g_buffers[0];
 return 0;
}
