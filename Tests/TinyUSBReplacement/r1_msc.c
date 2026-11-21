#include "../../ThirdParty/tinyusb/src/class/msc/msc_device.c"
bool R1_MscResetRejected(void) {
 tusb_control_request_t r;memset(&r,0,sizeof(r));r.bmRequestType=0x21;r.bRequest=MSC_REQ_RESET;
 _mscd_itf.rhport=0;_mscd_itf.ep_in=0x82;_mscd_itf.ep_out=2;_mscd_itf.stage=MSC_STAGE_DATA;
 return !mscd_control_xfer_cb(0,CONTROL_STAGE_SETUP,&r) && _mscd_itf.stage==MSC_STAGE_DATA;
}
