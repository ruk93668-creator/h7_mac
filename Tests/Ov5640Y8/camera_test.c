/* Current production code; only RCC/GPIO/DWT time are simulated. */
#include "../../CM7/BSP/bsp_camera.c"
#include "../../CM7/Device/Src/device.c"
#include "../../CM7/Device/Src/ov5640_device.c"
#include "../../CM7/Application/app_camera.c"
#include "../../CM7/Application/app_main.c"
#include "../../CM7/Framework/framework_timer.c"
#include "../../CM7/Framework/framework_event.c"
#include "../../CM7/Framework/framework_runtime.c"
#include "../../CM7/Framework/framework_init.c"

typedef struct {
 uint32_t fail_line,checks,tx_count,starts,stops,nacks,reads,reset_high,mco,delays;
 uint32_t count,passed,failed,done,heap,last_bus_error;
 int32_t init_result;
} TestResult;
volatile TestResult g_result;
uint8_t g_wire_tx[128];
uint32_t SystemCoreClock;
static uint8_t registers[65536];
static int32_t mismatch_reg = -1,fail_reg = -1;
static uint32_t writes,verify_active,fail_read;
static uint32_t now,clock_q,host_sda,host_scl,bus_active;
static uint32_t phase,cycle,acc,byte_index;
static uint32_t read_bit,sensor_id,reg_pointer,reg_high;
static uint32_t current_len,packet_first,stuck_sda,stuck_scl;
static uint32_t ack_stage;
static uint32_t gpio_b_calls;
static int32_t nack_index;

void *memcpy(void *dst,const void *src,size_t n){uint8_t *d=dst;const uint8_t *s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int v,size_t n){uint8_t *d=dst;while(n--)*d++=(uint8_t)v;return dst;}
void __aeabi_memclr4(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr8(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memcpy(void *dst,const void *src,size_t n){volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memcpy4(void *dst,const void *src,size_t n){volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
static int Check(uint32_t line,int32_t a,int32_t b){g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Check(__LINE__,(int32_t)(a),(int32_t)(b)))return 1;}while(0)
uint32_t Platform_TimeGetMs(void){return now++;}
void SystemCoreClockUpdate(void){SystemCoreClock=480000000U;}
void HAL_RCCEx_GetPLL1ClockFreq(PLL1_ClocksTypeDef *c){c->PLL1_Q_Frequency=clock_q;}
void HAL_RCC_MCOConfig(uint32_t m,uint32_t src,uint32_t div)
{
 if(m!=RCC_MCO1 || src!=RCC_MCO1SOURCE_PLL1QCLK || div!=10U || g_result.reset_high)g_result.fail_line=__LINE__;
 g_result.mco++;
}
void HAL_Delay(uint32_t ms)
{
 if((g_result.delays==0 && (ms!=10 || g_result.reset_high)) || (g_result.delays==1 && (ms!=20 || !g_result.reset_high)))g_result.fail_line=__LINE__;
 g_result.delays++;now+=ms;
}
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *g)
{
 if(p==GPIOB)gpio_b_calls++;
 if(p==GPIOB && (g->Pin!=(GPIO_PIN_8|GPIO_PIN_9) || g->Mode!=GPIO_MODE_OUTPUT_OD || g->Pull!=GPIO_NOPULL))g_result.fail_line=__LINE__;
}
/* Bit-level sensor model: decode host bytes from GPIO clock edges, not from
 * BSP parameters. Stop preserves the selected register for the read phase. */
void HAL_GPIO_WritePin(void *p,uint16_t pins,GPIO_PinState state)
{
 uint32_t high=state==GPIO_PIN_SET;
 if(p==GPIOH){g_result.reset_high=high;return;}
 if(p!=GPIOB)return;
 if(pins&GPIO_PIN_8){
  if(host_sda!=high && host_scl){
   if(!high){bus_active=1;phase=0;cycle=0;acc=0;current_len=0;g_result.starts++;}
   else{bus_active=0;g_result.stops++;}
  }
  host_sda=high;
 }
 if(pins&GPIO_PIN_9){
  if(!host_scl && high && bus_active){
   if(phase==0){
    if(cycle<8){acc=(acc<<1)|host_sda;cycle++;
     if(cycle==8){byte_index=g_result.tx_count;
      if(byte_index<128)g_wire_tx[byte_index]=(uint8_t)acc;g_result.tx_count++;
      if(current_len==0)packet_first=acc;
      ack_stage=packet_first==0x79U?3U:current_len;
      if(current_len==1)reg_high=acc;
      if(current_len==2)reg_pointer=(reg_high<<8)|acc;
      if(current_len==3 && packet_first==0x78 && nack_index!=3 && (int32_t)reg_pointer!=fail_reg){
       if(reg_pointer==0x3008 && acc==0x82){uint32_t j;for(j=0;j<65536;j++)registers[j]=0;}
       registers[reg_pointer]=(uint8_t)acc;writes++;
       if(reg_pointer==0x503D && acc==0x80)verify_active=1;
      }
      current_len++;
     }
    }else cycle=9;
   }else if(phase==1){
    if(cycle<8){uint32_t value=reg_pointer==0x300A?((sensor_id>>8)&255):(reg_pointer==0x300B?(sensor_id&255):registers[reg_pointer]);if(verify_active && (int32_t)reg_pointer==mismatch_reg)value^=1;read_bit=(value>>(7-cycle))&1U;cycle++;}
    else{if(host_sda!=1)g_result.fail_line=__LINE__;g_result.nacks++;cycle=9;}
   }
  }
  if(host_scl && !high && bus_active && cycle==9){
   if(phase==0){phase=(current_len==1 && packet_first==0x79)?1U:0U;cycle=0;acc=0;}
   else{phase=2;g_result.reads++;}
  }
  host_scl=high;
 }
}
GPIO_PinState HAL_GPIO_ReadPin(void *p,uint16_t pin)
{
 (void)p;
 if(pin==GPIO_PIN_9)return (stuck_scl || !host_scl)?GPIO_PIN_RESET:GPIO_PIN_SET;
 if(stuck_sda)return GPIO_PIN_RESET;
 if(!bus_active)return host_sda?GPIO_PIN_SET:GPIO_PIN_RESET;
 if(phase==0 && cycle==9)return ((int32_t)ack_stage==nack_index || (ack_stage==3 && packet_first==0x78 && (int32_t)reg_pointer==fail_reg) || (fail_read && verify_active && packet_first==0x79))?GPIO_PIN_SET:GPIO_PIN_RESET;
 if(phase==1)return read_bit?GPIO_PIN_SET:GPIO_PIN_RESET;
 return host_sda?GPIO_PIN_SET:GPIO_PIN_RESET;
}

static int Cases(uint32_t id)
{
 uint32_t i,n;
 uint8_t value=0,bytes[2]={0xA5,0x5A};
 Ov5640ConfigReport_t report;
 SysResult_t result;
 clock_q=240000000;host_sda=host_scl=1;sensor_id=0x5640;nack_index=-1;mismatch_reg=fail_reg=-1;
 if(id==25){EQ(BSP_Camera_WriteReg(0x1234,0xAB),SYS_ERR_INVALID_STATE);EQ(writes,0);return 0;}
 if(id==35)clock_q=1;
 if(id==35 || id==36){EQ(Framework_InitRuntime(),id==35?SYS_ERR_INVALID_STATE:SYS_ERR_NOT_SUPPORTED);EQ(writes,0);return 0;}
 EQ(BSP_Camera_Init(),SYS_OK);
 if(id>=20 && id<=24){
  if(id<24)nack_index=(int32_t)id-20;
  result=BSP_Camera_WriteReg(0x1234,0xAB);
  EQ(result,id==24?SYS_OK:SYS_ERR_IO);EQ(bus_active,0);EQ(host_sda,1);EQ(host_scl,1);
  if(id==24){EQ(registers[0x1234],0xAB);EQ(BSP_Camera_ReadReg(0x1234,&value),SYS_OK);EQ(value,0xAB);}
  else{EQ(s_sccb_error,id==23?0x40:1U<<(id-18));EQ(writes,0);}
  EQ(g_wire_tx[0],0x78);
  if(id>=21)EQ(g_wire_tx[1],0x12);
  if(id>=22)EQ(g_wire_tx[2],0x34);
  if(id>=23)EQ(g_wire_tx[3],0xAB);
  return 0;
 }
 if(id==26 || id==27){stuck_sda=id==26;stuck_scl=id==27;EQ(BSP_Camera_WriteReg(0x1234,0xAB),SYS_ERR_BUSY);EQ(writes,0);return 0;}
 if(id==28){n=now;EQ(BSP_Camera_WriteReg(0x3008,0x82),SYS_OK);EQ(now-n,5);EQ(g_result.delays,3);return 0;}
 if(id==29){
  s_bus_result=SYS_OK;EQ(Ov5640_BusWrite(0x78,0x1234,bytes,2),SYS_OK);EQ(registers[0x1234],0xA5);EQ(registers[0x1235],0x5A);
  bytes[0]=bytes[1]=0;EQ(Ov5640_BusRead(0x78,0x1234,bytes,2),SYS_OK);EQ(bytes[0],0xA5);EQ(bytes[1],0x5A);
  EQ(Ov5640_BusRead(0x78,0xFFFF,bytes,2),SYS_ERR_INVALID_ARG);s_bus_result=SYS_OK;
  EQ(Ov5640_BusRead(0x79,0,bytes,1),SYS_ERR_INVALID_ARG);s_bus_result=SYS_OK;
  EQ(Ov5640_BusRead(0x78,0,NULL,1),SYS_ERR_INVALID_ARG);s_bus_result=SYS_OK;
  EQ(Ov5640_BusRead(0x78,0,bytes,0),SYS_ERR_INVALID_ARG);return 0;
 }
 if(id==34){
  EQ(s_camera_ops.configure_y8_colorbar(NULL,&report),SYS_ERR_INVALID_ARG);
  EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640,NULL),SYS_ERR_INVALID_ARG);
  EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640,&report),SYS_ERR_INVALID_STATE);
  s_ov5640.base.state=DEVICE_STATE_BUSY;EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640,&report),SYS_ERR_BUSY);EQ(writes,0);return 0;
 }
 if(id==37){nack_index=3;EQ(BSP_Camera_ReadReg(0x300A,&value),SYS_ERR_IO);EQ(s_sccb_error,0x20);return 0;}
 if(id==1)sensor_id=0x1234;
 if(id>=3 && id<=19)mismatch_reg=s_verify[id-3].reg;
 if(id==30 || id==32)fail_reg=0x3036;
 if(id==31)fail_read=1;
 if(id==2)nack_index=0;
 result=Framework_InitRuntime();g_result.init_result=result;
 EQ(g_camera_report.done,1);EQ(g_camera_report.result,result);EQ(s_ov5640.base.state,DEVICE_STATE_READY);
 EQ(s_heap_size,0);EQ(s_timer_pool_count,0);EQ(gpio_b_calls,1);
 if(id==1 || (id>=3 && id<=19)){
  EQ(result,SYS_ERR_PROTOCOL);EQ(g_camera_report.sensor.configured,0);
  if(id==1){EQ(writes,0);EQ(g_camera_report.sensor.chip_id,0x1234);}
  else{EQ(g_camera_report.sensor.failed_reg,s_verify[id-3].reg);EQ(g_camera_report.sensor.checked_count,id-3);}
  return 0;
 }
 if(id==2 || id==30 || id==31 || id==32){
  EQ(result,SYS_ERR_IO);EQ(g_camera_report.sensor.configured,0);
  if(id!=32)return 0;
  fail_reg=-1;EQ(App_Init(),SYS_OK);
 }
 else EQ(result,SYS_OK);
 EQ(g_camera_report.sensor.chip_id,0x5640);EQ(g_camera_report.sensor.width,160);EQ(g_camera_report.sensor.height,120);
 EQ(g_camera_report.sensor.checked_count,OV5640_VERIFY_COUNT);EQ(g_camera_report.sensor.configured,1);
 for(i=0;i<OV5640_VERIFY_COUNT;i++)EQ(registers[s_verify[i].reg],s_verify[i].value);
 EQ(registers[0x3037],0x03);EQ(registers[0x3008],0x02);
 n=writes;
 for(i=0;i<100;i++){Framework_Run();App_Run();}
 EQ(App_Init(),SYS_OK);EQ(writes,n);EQ(s_heap_size,0);EQ(s_timer_pool_count,0);
 return 0;
}
int Camera_TestRun(uint32_t id)
{
 int result=Cases(id);
 g_result.count=writes;g_result.heap=s_heap_size;g_result.last_bus_error=s_sccb_error;
 return result;
}
