#include "../../ThirdParty/tinyusb/src/device/usbd.c"
extern bool R1_MscResetRejected(void);
extern void R1_Seed(uint8_t ep);extern bool R1_Held(uint8_t ep);
volatile uint32_t g_result[8];
#define CK(x) do {++g_result[1];if(!(x)){g_result[0]=__LINE__;return;}}while(0)
uint32_t tusb_time_millis_api(void){return 0;}
void Test_Run(uint32_t which){
 uint8_t a=which==1?0x82:2; uint8_t *state=&_usbd_dev.ep_status[a&15][a>>7];
 _usbd_rhport=0;_usbd_dev.cfg_num=1;*state=TU_EDPT_STATE_BUSY|TU_EDPT_STATE_CLAIMED;
 R1_Seed(a);CK(!usbd_edpt_abort(0,a));CK(_usbd_abort_failed);CK(R1_Held(a));
 CK((*state&(TU_EDPT_STATE_BUSY|TU_EDPT_STATE_CLAIMED))==(TU_EDPT_STATE_BUSY|TU_EDPT_STATE_CLAIMED));
 CK(!usbd_edpt_abort(0,a));usbd_edpt_clear_stall(0,a);usbd_edpt_close(0,a);
 CK(usbd_edpt_busy(0,a));CK(!usbd_edpt_release(0,a));CK(!usbd_edpt_xfer(0,a,NULL,0,false));
 usbd_reset(0);CK(_usbd_dev.cfg_num==1);CK(!tud_deinit(0));
 {tusb_rhport_init_t i={.role=TUSB_ROLE_DEVICE,.speed=TUSB_SPEED_FULL};CK(!tud_rhport_init(0,&i));}
 {dcd_event_t e;memset(&e,0,sizeof(e));e.event_id=DCD_EVENT_XFER_COMPLETE;e.xfer_complete.ep_addr=a;CK(!usbd_process_event(&e));}
 CK(R1_Held(a));CK(usbd_edpt_busy(0,a));
 if(which==1) CK(R1_MscResetRejected());CK(!dcd_deinit(0));CK(!dcd_edpt_xfer(0,a,NULL,0,false));
}
