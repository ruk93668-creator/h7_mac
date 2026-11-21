#include "../../ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c"
void R1B_Open(uint8_t ep) {
 if (_dcd_data.dfifo_top==0) {dfifo_device_init(0);xfer_status[0][0].max_size=64;xfer_status[0][1].max_size=64;}
 xfer_ctl_t *x=XFER_CTL_BASE(ep&15,ep>>7);x->max_size=64;
 DWC2_REG(0)->ep[(ep>>7)?0:1][ep&15].ctl=EPCTL_USBAEP | (DEPCTL_EPTYPE_BULK<<18) | 64;
 DWC2_REG(0)->daintmsk |= TU_BIT((ep&15)+((ep>>7)?0:16));
 DWC2_REG(0)->gintmsk |= GINTMSK_RXFLVLM | GINTMSK_IEPINT | GINTMSK_USBRST;
 DWC2_REG(0)->diepmsk |= 0x43U;
}
void R1B_Observe(uint8_t ep,volatile uint32_t *v) {
 bulk_cancel_t *c=&bulk_cancel[ep&15][ep>>7];
 v[0]=c->phase;v[1]=XFER_CTL_BASE(ep&15,ep>>7)->buffer!=NULL;v[2]=transfer_generation[ep&15][ep>>7];
 v[3]=dcd_device_faulted(0);v[4]=(uint32_t)(int32_t)out_nak_owner;
}
uint32_t SystemCoreClock=480000000U;
