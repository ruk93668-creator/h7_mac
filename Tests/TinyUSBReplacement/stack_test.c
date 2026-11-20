/* Real TinyUSB USBD + MSC; only this file's DCD is a software host boundary. */
#include "tusb.h"
#include "device/dcd.h"
#include "tinyusb_ram.h"
#include <string.h>
volatile uint32_t g_result[8];
typedef struct { uint8_t *buffer; uint16_t size; bool armed,stalled; uint32_t arms; } Ep;
static Ep ep[2][9];
static uint32_t clocks,address,opens,deinits,aborts;
static Ep *Endpoint(uint8_t a) { return &ep[a>>7][a&15]; }
#define CHECK(v) do { ++g_result[1]; if (!(v)) { g_result[0]=__LINE__; return; } } while (0)
uint32_t tusb_time_millis_api(void) { return clocks++; }
bool dcd_init(uint8_t p,const tusb_rhport_init_t *c) { (void)p;(void)c;return true; }
bool dcd_deinit(uint8_t p) { (void)p;++deinits;memset(ep,0,sizeof(ep));return true; }
void dcd_int_enable(uint8_t p) { (void)p; }
void dcd_int_disable(uint8_t p) { (void)p; }
void dcd_int_handler(uint8_t p) { (void)p; }
void dcd_connect(uint8_t p) { (void)p; }
void dcd_disconnect(uint8_t p) { (void)p; }
void dcd_remote_wakeup(uint8_t p) { (void)p; }
void dcd_sof_enable(uint8_t p,bool en) { (void)p;(void)en; }
bool dcd_edpt_xfer(uint8_t p,uint8_t a,uint8_t *b,uint16_t n,bool isr)
{ Ep *e=Endpoint(a);(void)p;(void)isr;e->buffer=b;e->size=n;e->armed=true;++e->arms;return true; }
void dcd_set_address(uint8_t p,uint8_t a) { address=a;(void)dcd_edpt_xfer(p,0x80,NULL,0,false); }
bool dcd_edpt_open(uint8_t p,const tusb_desc_endpoint_t *d) { (void)p;(void)d;++opens;return true; }
void dcd_edpt_close_all(uint8_t p) { uint8_t i;(void)p;for(i=1;i<9;++i) { ep[0][i].armed=false;ep[1][i].armed=false; } }
void dcd_edpt_stall(uint8_t p,uint8_t a) { Ep *e=Endpoint(a);(void)p;e->stalled=true;e->armed=false; }
void dcd_edpt_clear_stall(uint8_t p,uint8_t a) { (void)p;Endpoint(a)->stalled=false; }
/* false invokes TinyUSB's actual stall/clear fallback, not a fabricated success. */
bool dcd_edpt_abort_xfer(uint8_t p,uint8_t a) { (void)p;(void)a;++aborts;return false; }
bool dcd_edpt_iso_alloc(uint8_t p,uint8_t a,uint16_t n) { (void)p;(void)a;(void)n;return false; }
bool dcd_edpt_iso_activate(uint8_t p,const tusb_desc_endpoint_t *d) { (void)p;(void)d;return false; }
static void Pump(void) { (void)tud_task_ext(0,false); }
static void Complete(uint8_t a) { Ep *e=Endpoint(a);uint32_t n=e->size;e->armed=false;dcd_event_xfer_complete(0,a,n,XFER_RESULT_SUCCESS,false);Pump(); }
static void Setup(uint8_t type,uint8_t req,uint16_t value,uint16_t index,uint16_t size)
{ tusb_control_request_t r;memset(&r,0,sizeof(r));r.bmRequestType=type;r.bRequest=req;r.wValue=value;r.wIndex=index;r.wLength=size;
 ep[0][0].armed=ep[1][0].armed=false;ep[0][0].stalled=ep[1][0].stalled=false;
 dcd_event_setup_received(0,(uint8_t *)&r,false);Pump(); }
static void Configure(void) { Setup(0,9,1,0,0);if(ep[1][0].armed)Complete(0x80); }
static void Command(uint8_t cmd,uint32_t bytes,bool in,uint16_t blocks,uint32_t tag,uint8_t cdb_len)
{ msc_cbw_t cbw;memset(&cbw,0,sizeof(cbw));cbw.signature=MSC_CBW_SIGNATURE;cbw.tag=tag;cbw.total_bytes=bytes;cbw.dir=in?0x80:0;cbw.cmd_len=cdb_len;
 cbw.command[0]=cmd;cbw.command[7]=(uint8_t)(blocks>>8);cbw.command[8]=(uint8_t)blocks;
 if(cmd==3||cmd==0x12)cbw.command[4]=(uint8_t)bytes;
 if(ep[0][2].armed && ep[0][2].size==31) { memcpy(ep[0][2].buffer,&cbw,31);Complete(2); }
 else g_result[7]++;
}
static bool Csw(uint32_t tag,uint8_t status,uint32_t residue)
{ msc_csw_t c;if(!ep[1][2].armed || ep[1][2].size!=13)return false;memcpy(&c,ep[1][2].buffer,13);if(c.signature!=MSC_CSW_SIGNATURE||c.tag!=tag||c.status!=status||c.data_residue!=residue)return false;Complete(0x82);return true; }
void Test_Run(uint32_t which)
{
 tusb_rhport_init_t init={.role=TUSB_ROLE_DEVICE,.speed=TUSB_SPEED_FULL};uint32_t i,arms;
 memset((void *)g_result,0,sizeof(g_result));CHECK(tusb_rhport_init(0,&init));dcd_event_bus_reset(0,TUSB_SPEED_FULL,false);Pump();
 if(which==1) {
  Setup(0x80,6,0x100,0,18);CHECK(ep[1][0].size==18);CHECK(ep[1][0].buffer[1]==1);Complete(0x80);CHECK(ep[0][0].armed&&ep[0][0].size==0);Complete(0);
  Setup(0,5,7,0,0);CHECK(address==7);CHECK(ep[1][0].armed&&ep[1][0].size==0);Complete(0x80);
 }
 Configure();CHECK(tud_mounted());CHECK(opens==2);CHECK(ep[0][2].armed&&ep[0][2].size==31);
 if(which==1) { Setup(0xa1,0xfe,0,0,1);CHECK(ep[1][0].size==1&&ep[1][0].buffer[0]==0); }
 if(which==2) { Command(0x12,36,true,0,2,6);CHECK(ep[1][2].armed&&ep[1][2].size==36);CHECK(memcmp(ep[1][2].buffer+8,"H7 TEST ",8)==0);Complete(0x82);CHECK(Csw(2,0,0)); }
 if(which==3) { Command(0x28,1536,true,3,3,10);CHECK(!TinyUSB_RamDrained());CHECK(!ep[1][2].armed);TinyUSB_RamPoll();Pump();CHECK(ep[1][2].size==1024);for(i=0;i<1024;++i)CHECK(ep[1][2].buffer[i]==0);Complete(0x82);CHECK(!TinyUSB_RamDrained());TinyUSB_RamPoll();Pump();CHECK(ep[1][2].size==512);Complete(0x82);CHECK(Csw(3,0,0));CHECK(TinyUSB_RamDrained()); }
 if(which==4) { Command(0x2a,512,false,1,4,10);CHECK(ep[0][2].size==512);memset(ep[0][2].buffer,0x5a,512);Complete(2);CHECK(!TinyUSB_RamDrained());TinyUSB_RamPoll();Pump();CHECK(Csw(4,0,0));Command(0x28,512,true,1,5,10);TinyUSB_RamPoll();Pump();CHECK(ep[1][2].size==512);for(i=0;i<512;++i)CHECK(ep[1][2].buffer[i]==0x5a);Complete(0x82);CHECK(Csw(5,0,0)); }
 if(which==5) { Command(0x7f,0,false,0,60,6);CHECK(Csw(60,1,0));Command(3,18,true,0,6,12);CHECK(ep[1][2].size==18);CHECK((ep[1][2].buffer[0]&0x7f)==0x70&&ep[1][2].buffer[2]==5&&ep[1][2].buffer[7]==10&&ep[1][2].buffer[12]==0x20);Complete(0x82);CHECK(Csw(6,0,0)); }
 if(which==6 || which==7 || which==8 || which==10) {
  Command(0x28,512,true,1,9,10);CHECK(!TinyUSB_RamDrained());arms=ep[1][2].arms;
  if(which==7||which==10) { dcd_event_bus_reset(0,TUSB_SPEED_FULL,false);Pump();Configure(); }
  else { Setup(0x21,0xff,0,0,0);CHECK(ep[1][0].size==0);Complete(0x80); }
  if(which==8) { Setup(0x21,0xff,0,0,0);Complete(0x80);Setup(2,1,0,2,0);Complete(0x80);Setup(2,1,0,0x82,0);Complete(0x80); }
  CHECK(!TinyUSB_RamDrained());CHECK(!ep[0][2].armed);TinyUSB_RamPoll();Pump();CHECK(TinyUSB_RamDrained());CHECK(ep[1][2].arms==arms);CHECK(ep[0][2].armed&&ep[0][2].size==31);
  TinyUSB_RamPoll();Pump();CHECK(ep[0][2].size==31);CHECK(!tud_msc_async_io_done(512,false));Command(0x25,8,true,0,10,10);CHECK(ep[1][2].size==8);Complete(0x82);CHECK(Csw(10,0,0));
 }
 if(which==9) { CHECK(TinyUSB_RamDrained());CHECK(tud_task_stop());CHECK(tud_task_ext(0,false));CHECK(tud_deinit(0));CHECK(deinits==1);CHECK(tusb_rhport_init(0,&init));dcd_event_bus_reset(0,TUSB_SPEED_FULL,false);Pump();Configure();CHECK(tud_mounted()); }
 if(which==12) { Command(0x28,512,true,1,12,10);TinyUSB_RamPoll();Pump();CHECK(ep[1][2].armed&&ep[1][2].size==512);Setup(0x21,0xff,0,0,0);Complete(0x80);CHECK(aborts==1);CHECK(!ep[1][2].armed);CHECK(ep[0][2].armed&&ep[0][2].size==31);Command(0x25,8,true,0,13,10);CHECK(ep[1][2].size==8);Complete(0x82);CHECK(Csw(13,0,0)); }
 if(which==11) { Setup(0x80,6,0x100,0,18);CHECK(ep[1][0].size==18);Setup(0x80,6,0x200,0,9);CHECK(ep[1][0].size==9);CHECK(ep[1][0].buffer[1]==2); }
 g_result[2]=ep[1][2].arms;g_result[3]=aborts;g_result[4]=deinits;g_result[5]=TinyUSB_RamDrained();CHECK(g_result[7]==0);
}
