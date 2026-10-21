/* Current production code; only RCC/GPIO/DWT time are simulated. */
#include "../../CM7/BSP/bsp_camera.c"
#include "../../CM7/Device/Src/device.c"
#include "../../CM7/Device/Src/ov5640_device.c"
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
uint32_t Platform_TimeGetMs(void){return now;}
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
      current_len++;
     }
    }else cycle=9;
   }else if(phase==1){
    if(cycle<8){uint32_t value=reg_pointer==0x300A?((sensor_id>>8)&255):(sensor_id&255);read_bit=(value>>(7-cycle))&1U;cycle++;}
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
 if(phase==0 && cycle==9)return (int32_t)ack_stage==nack_index?GPIO_PIN_SET:GPIO_PIN_RESET;
 if(phase==1)return read_bit?GPIO_PIN_SET:GPIO_PIN_RESET;
 return host_sda?GPIO_PIN_SET:GPIO_PIN_RESET;
}
static void Step(uint32_t tick){now=tick;Framework_Run();App_Run();}
static int Cases(uint32_t id)
{
 uint32_t i,start;
 uint16_t chip_id=0;
 SysResult_t expected=SYS_OK;
 uint8_t value=0xEE;
 clock_q=240000000U;host_sda=host_scl=1;sensor_id=0x5640;nack_index=-1;
 if(id==5)clock_q=480000000U;
 if(id==13){EQ(BSP_Camera_ReadReg(0x300A,&value),SYS_ERR_INVALID_STATE);EQ(BSP_Camera_ReadReg(0x300A,NULL),SYS_ERR_INVALID_ARG);return 0;}
 if(id==15){EQ(App_Init(),SYS_OK);App_Run();EQ(s_heap_size,0);EQ(s_timer_pool_count,0);EQ(g_result.tx_count,0);return 0;}
 BSP_Camera_EarlyInit();EQ(gpio_b_calls,1);
 g_result.init_result=Framework_InitRuntime();
 if(id==5 || id==6){
  EQ(g_result.init_result,id==5?SYS_ERR_INVALID_STATE:SYS_ERR_NOT_SUPPORTED);EQ(g_result.reset_high,0);return 0;
 }
 EQ(g_result.init_result,SYS_OK);EQ(g_result.mco,1);EQ(g_result.delays,2);EQ(g_result.reset_high,1);
 EQ(gpio_b_calls,1);EQ(s_ov5640.base.state,DEVICE_STATE_READY);
 EQ(s_timer_pool_count,0);EQ(s_heap_size,0);EQ(g_result.tx_count,0);
 EQ(g_framework_init_BSP_Camera_Init.level,INIT_LEVEL_BUS);EQ(g_framework_init_App_Init.level,INIT_LEVEL_APPLICATION);
 if(id==7){
  EQ(s_camera_ops.read_id(NULL,&chip_id),SYS_ERR_INVALID_ARG);
  EQ(s_camera_ops.read_id(&s_ov5640,NULL),SYS_ERR_INVALID_ARG);
  s_ov5640.base.state=DEVICE_STATE_UNINIT;EQ(s_camera_ops.read_id(&s_ov5640,&chip_id),SYS_ERR_INVALID_STATE);return 0;
 }
 if(id==12)return 0;
 if(id==11){
  start=now;
  for(i=1;i<=20;i++)Step(start+i*1000U);
  EQ(g_result.tx_count,0);EQ(g_result.starts,0);EQ(s_heap_size,0);EQ(s_timer_pool_count,0);return 0;
 }
 if(id==1 || (id>=8 && id<=10)){nack_index=id==1?0:(int32_t)id-7;expected=SYS_ERR_IO;}
 if(id==2){sensor_id=0x1234;expected=SYS_ERR_PROTOCOL;}
 if(id==4){stuck_sda=1;expected=SYS_ERR_BUSY;}
 if(id==16){stuck_scl=1;expected=SYS_ERR_BUSY;}
 //连续读取仅由独立测试主动调用Device Ops，正式Application没有测试循环
 for(i=0;i<10;i++){
  if(id==3){nack_index=i<2?0:-1;expected=i<2?SYS_ERR_IO:SYS_OK;}
  EQ(s_camera_ops.read_id(&s_ov5640,&chip_id),expected);
  g_result.count++;
  if(expected==SYS_OK){EQ(chip_id,0x5640);g_result.passed++;}
  else g_result.failed++;
  if(id==2)EQ(chip_id,0x1234);
 }
 EQ(s_heap_size,0);EQ(s_timer_pool_count,0);
 start=g_result.tx_count;Step(now+100000U);EQ(g_result.tx_count,start);
 if(id==0 || id==14){
  EQ(g_result.passed,10);EQ(g_result.tx_count,80);EQ(g_result.starts,40);EQ(g_result.stops,40);EQ(g_result.nacks,20);
  for(i=0;i<10;i++){
   uint32_t n=i*8;
   EQ(g_wire_tx[n],0x78);EQ(g_wire_tx[n+1],0x30);EQ(g_wire_tx[n+2],0x0A);EQ(g_wire_tx[n+3],0x79);
   EQ(g_wire_tx[n+4],0x78);EQ(g_wire_tx[n+5],0x30);EQ(g_wire_tx[n+6],0x0B);EQ(g_wire_tx[n+7],0x79);
  }
 }
 if(id==3){EQ(g_result.passed,8);EQ(g_result.failed,2);}
 if(id==1)EQ(s_sccb_error,4);
 if(id>=8 && id<=10)EQ(s_sccb_error,1U<<(id-5U));
 if(id==4)EQ(s_sccb_error,1);
 if(id==16)EQ(s_sccb_error,2);
 return 0;
}
uint32_t Camera_TestRun(uint32_t id)
{
 uint32_t failed=(uint32_t)Cases(id);
 g_result.heap=s_heap_size;g_result.last_bus_error=s_sccb_error;
 return failed;
}
