#include "../../ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c"
static uint8_t held[64];
void R1_Seed(uint8_t ep) { xfer_ctl_t *x=XFER_CTL_BASE(ep&15,ep>>7); x->buffer=held;x->max_size=64;x->total_len=64; }
bool R1_Held(uint8_t ep) { return XFER_CTL_BASE(ep&15,ep>>7)->buffer==held; }
uint32_t SystemCoreClock=480000000U;
