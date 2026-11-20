//Independent HAL hook tests: no BSP Stop code participates in these tests.
#include "../UsbBulkOut/hal_out_evidence.c"
volatile uint32_t g_hook[20];
static uint32_t s_cookie, s_deny, s_suppress;
static uint32_t HookRxBegin(PCD_HandleTypeDef *h,uint32_t status,uint8_t *dst,uint16_t count,uint32_t *cookie)
{
 uint32_t ep=status&15U,kind=(status>>17)&15U;
 ++g_hook[2];*cookie=++s_cookie;g_hook[6]=status;
 if(s_deny || (kind==2U && (ep>=9U || h->OUT_ep[ep].xfer_count>64U || count>64U-h->OUT_ep[ep].xfer_count || dst!=g_buffers[ep]+h->OUT_ep[ep].xfer_count)) ||
    (kind==6U && (ep!=0U || count!=8U || dst!=(uint8_t*)h->Setup))) {++g_hook[5];return 0U;}
 if(count) ++g_hook[1];return 1U;
}
static void HookRxEnd(PCD_HandleTypeDef *h,uint32_t status,uint32_t cookie,uint16_t count,uint32_t copied)
{(void)h;(void)status;(void)cookie;++g_hook[3];if(g_hook[1])--g_hook[1];if(copied)g_hook[4]+=count;}
static uint32_t HookOutBegin(PCD_HandleTypeDef *h,uint8_t ep,uint32_t events,uint32_t *cookie)
{uint32_t USBx_BASE=(uint32_t)h->Instance;*cookie=++s_cookie;g_hook[7]=USBx_OUTEP(ep)->DOEPINT;g_hook[16]=events;return s_suppress?0U:PCD_LC_FORWARD_XFRC;}
static void HookOutEnd(PCD_HandleTypeDef *h,uint8_t ep,uint32_t events,uint32_t cookie)
{uint32_t USBx_BASE=(uint32_t)h->Instance;(void)events;g_hook[8]=USBx_OUTEP(ep)->DOEPINT;g_hook[17]=cookie;++g_hook[18];}
static void HookNak(PCD_HandleTypeDef *h,uint32_t action,uint8_t ep)
{(void)h;(void)ep;if(action==PCD_LC_NAK_ISO_REQUEST)++g_hook[10];else ++g_hook[11];}
static void HookBoundary(PCD_HandleTypeDef *h,uint32_t raw)
{if((raw&USB_OTG_GINTSTS_USBRST)||((raw&USB_OTG_GINTSTS_OTGINT)&&(h->Instance->GOTGINT&USB_OTG_GOTGINT_SEDET))) {++g_hook[12];g_hook[13]=1U;s_deny=1U;}}
static uint32_t HookDetach(PCD_HandleTypeDef *h) {(void)h;return g_hook[0]==0U && g_hook[1]==0U;}
static const PCD_LifecycleHooks s_test_hooks={PCD_LIFECYCLE_VERSION,HookRxBegin,HookRxEnd,HookOutBegin,HookOutEnd,HookNak,HookBoundary,HookDetach};
int Hook_Step(uint32_t op,uint32_t arg)
{
 if(op==0U) {C0_Step(0U,0U);g_pcd.State=HAL_PCD_STATE_READY;g_hook[0]=1U;}
 else if(op==2U) g_hook[9]=HAL_PCD_SetLifecycleHooks(&g_pcd,&s_test_hooks);
 else if(op==3U) {PCD_LifecycleHooks partial=s_test_hooks;partial.RxEnd=NULL;g_hook[9]=HAL_PCD_SetLifecycleHooks(&g_pcd,&partial);}
 else if(op==4U) s_deny=arg;
 else if(op==5U) g_hook[9]=HAL_PCD_SetLifecycleHooks(&g_pcd,NULL);
 else if(op==6U) g_hook[0]=0U;
 else if(op==7U) s_suppress=1U;
 else if(op==11U) C0_Step(6U,arg);
 else if(op==12U) C0_Step(7U,arg);
 else if(op==10U) g_pcd.Init.dma_enable=1U;
 else if(op==18U) C0_Step(5U,arg);
 else if(op==9U) g_pcd.OUT_ep[2].xfer_buff=NULL;
 else C0_Step(op,arg);
 g_hook[19]=g_pcd.Lifecycle!=NULL;
 return 0;
}
