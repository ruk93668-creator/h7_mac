/* Include the actual modified DCD to expose its static legacy stop function for evidence. */
#include "../../ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c"
volatile uint32_t g_dcd_result[4];
static uint8_t old_buffer[64];
void usbd_spin_lock(bool isr) { (void)isr; }
void usbd_spin_unlock(bool isr) { (void)isr; }
void Dwc2_Run(uint32_t which)
{
 dwc2_regs_t *d=DWC2_REG(0);g_dcd_result[0]=1;
 if(which<=4) {
  uint8_t addr=which<=2 ? 2U : 0x82U;
  xfer_status[2][which<=2?0:1].buffer=old_buffer;
  edpt_disable(0,addr,true,false);
 } else if(which==5) edpt_disable(0,0,false,false);
 else if(which==6) {
  xfer_status[0][1].max_size=64;
  g_dcd_result[1]=dcd_edpt_xfer(0,0x80,NULL,0,false);
 } else if(which==7) { dfifo_flush_tx(d,2); }
 else if(which==8) { (void)dcd_deinit(0); }
 if(which==9)g_dcd_result[1]=dfifo_flush_rx(d);
 g_dcd_result[2]=dwc2_wait_faulted();g_dcd_result[0]=2;
}

uint32_t SystemCoreClock=480000000U;
void dcd_event_handler(dcd_event_t const *event,bool isr) { (void)event;(void)isr;++g_dcd_result[3]; }

uint32_t tusb_time_millis_api(void) {return 0;}
