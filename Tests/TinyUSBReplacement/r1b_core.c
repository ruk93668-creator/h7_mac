#include "../../ThirdParty/tinyusb/src/device/usbd.c"
#include "tinyusb_ram.h"
extern void R1B_Open(uint8_t ep);extern void R1B_Observe(uint8_t ep,volatile uint32_t *v);
volatile uint32_t g_observation[14];
static uint8_t buffer[64],next_buffer[64];static uint8_t address;static uint32_t tick,old_generation;
uint32_t tusb_time_millis_api(void) {return tick;}
void R1B_Step(uint32_t op,uint32_t arg) {
 int result=0;
 if(op==0) {_usbd_q=osal_queue_create(&_usbd_qdef);_usbd_dev.speed=TUSB_SPEED_FULL;address=(uint8_t)arg;_usbd_rhport=0;R1B_Open(address);memset(buffer,0xa5,sizeof(buffer));}
 if(op==1) {result=usbd_edpt_xfer(0,address,buffer,64,false);old_generation=dcd_edpt_generation(0,address);}
 if(op==2) result=usbd_edpt_abort(0,address);
 if(op==3) dcd_int_handler(0);
 if(op==4) tick+=arg;
 if(op==5) result=usbd_edpt_xfer(0,address,next_buffer,64,false);
 if(op==6) {dcd_event_t e;memset(&e,0,sizeof(e));e.event_id=DCD_EVENT_XFER_COMPLETE;e.xfer_complete.ep_addr=address;e.xfer_complete.generation=old_generation;result=usbd_process_event(&e);}
 if(op==7) {R1B_Open(1);result=usbd_edpt_xfer(0,1,next_buffer,64,false);}
 if(op==8) (void)tud_task_ext(0,false);
 if(op==9) {configuration_reset(0);mscd_init();result=process_set_config(0,1);_usbd_dev.cfg_num=1;}
 if(op==10) TinyUSB_RamPoll();
 if(op==11) result=tud_deinit(0);
 if(op==12) {tusb_rhport_init_t i={.role=TUSB_ROLE_DEVICE,.speed=TUSB_SPEED_FULL};result=tud_rhport_init(0,&i);}
 if(op==13) result=usbd_edpt_xfer(0,0x80,buffer,18,false);
 if(op==14) usbd_edpt_clear_stall(0,address);
 if(op==15) result=usbd_edpt_release(0,address);
 if(op==16) {dcd_event_t e;memset(&e,0,sizeof(e));e.event_id=DCD_EVENT_XFER_COMPLETE;e.xfer_complete.ep_addr=address;e.xfer_complete.generation=old_generation;dcd_event_handler(&e,true);}
 g_observation[0]=(uint32_t)result;g_observation[1]=usbd_edpt_busy(0,address);
 R1B_Observe(address,&g_observation[2]);g_observation[7]=buffer[0];g_observation[8]=next_buffer[0];
 g_observation[9]=dcd_device_faulted(0);g_observation[10]=_usbd_abort_failed;g_observation[11]=tick;g_observation[12]=dcd_edpt_generation(0,0x80);g_observation[13]=usbd_edpt_busy(0,2);
}
