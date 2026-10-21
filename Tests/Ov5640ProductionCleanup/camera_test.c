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
static uint32_t writes,verify_active,fail_read,verify_reads;
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
   else{phase=2;g_result.reads++;if(verify_active)verify_reads++;}
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

//回读验收仅存在于独立测试，不进入正式固件
#define TEST_VERIFY_COUNT 17U

typedef struct
{
 uint16_t failed_reg;
 uint8_t expected,actual,checked_count;
 uint8_t values[TEST_VERIFY_COUNT];
} TestVerifyReport;

//回读尺寸、格式、彩条、运行状态、时序和DVP输出
static const struct
{
	uint16_t reg;
	uint8_t value;
} s_test_verify[TEST_VERIFY_COUNT] =
{
	{0x3808U, 0x00U}, {0x3809U, 0xA0U},
	{0x380AU, 0x00U}, {0x380BU, 0x78U},
	{0x4300U, 0x10U}, {0x501FU, 0x00U},
	{0x503DU, 0x80U}, {0x5584U, 0x40U},
	{0x3008U, 0x02U},
	{0x380CU, 0x06U}, {0x380DU, 0x0EU},
	{0x380EU, 0x03U}, {0x380FU, 0xE8U},
	{0x3035U, 0x41U}, {0x3036U, 0x60U},
	{0x3017U, 0xFFU}, {0x3018U, 0xF3U}
};


static SysResult_t Test_VerifyRegisters(TestVerifyReport *report)
{
 uint32_t i;
 SysResult_t result;
 for(i=0;i<TEST_VERIFY_COUNT;i++){
  report->failed_reg=s_test_verify[i].reg;
  report->expected=s_test_verify[i].value;
  result=BSP_Camera_ReadReg(s_test_verify[i].reg,&report->values[i]);
  if(result!=SYS_OK)return result;
  report->actual=report->values[i];
  if(report->actual!=report->expected)return SYS_ERR_PROTOCOL;
  report->checked_count++;
 }
 report->failed_reg=0;
 return SYS_OK;
}

static int Cases(uint32_t id)
{
 uint32_t i,n;
 uint8_t value=0,bytes[2]={0xA5,0x5A};
 TestVerifyReport report={0};
 SysResult_t result,verified;
 clock_q=240000000;host_sda=host_scl=1;sensor_id=0x5640;nack_index=-1;mismatch_reg=fail_reg=-1;
 if(id==25){EQ(BSP_Camera_WriteReg(0x1234,0xAB),SYS_ERR_INVALID_STATE);EQ(writes,0);return 0;}
 if(id==35)clock_q=1;
 if(id==35 || id==36){g_result.init_result=Framework_InitRuntime();EQ(g_result.init_result,id==35?SYS_ERR_INVALID_STATE:SYS_ERR_NOT_SUPPORTED);EQ(writes,0);return 0;}
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
  EQ(s_camera_ops.configure_y8_colorbar(NULL),SYS_ERR_INVALID_ARG);
  EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640),SYS_ERR_INVALID_STATE);
  s_ov5640.base.state=DEVICE_STATE_BUSY;EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640),SYS_ERR_BUSY);EQ(writes,0);return 0;
 }
 if(id==37){nack_index=3;EQ(BSP_Camera_ReadReg(0x300A,&value),SYS_ERR_IO);EQ(s_sccb_error,0x20);return 0;}
 if(id==39){EQ(App_Init(),SYS_ERR_NOT_FOUND);EQ(s_camera_initialized,0);EQ(writes,0);return 0;}
 if(id==40 || id==41){
  const Ov5640Ops_t empty_ops={Ov5640_ReadId,NULL};
  EQ(Device_RegisterAll(),SYS_OK);EQ(Device_InitAll(),SYS_OK);
  s_ov5640.ops=id==40?NULL:&empty_ops;
  EQ(App_Init(),SYS_ERR_NOT_SUPPORTED);EQ(s_camera_initialized,0);EQ(writes,0);
  s_ov5640.ops=&s_camera_ops;EQ(App_Init(),SYS_OK);EQ(s_camera_initialized,1);return 0;
 }
 if(id==1)sensor_id=0x1234;
 if(id>=3 && id<=19)mismatch_reg=s_test_verify[id-3].reg;
 if(id==30 || id==32)fail_reg=0x3036;
 if(id==31)fail_read=1;
 if(id==2)nack_index=0;
 result=Framework_InitRuntime();g_result.init_result=result;
 EQ(s_ov5640.base.state,DEVICE_STATE_READY);
 EQ(s_heap_size,0);EQ(s_timer_pool_count,0);EQ(gpio_b_calls,1);
 //正式初始化不再执行彩条开启后的验收读事务
 EQ(verify_reads,0);
 if(id==1){EQ(result,SYS_ERR_PROTOCOL);EQ(s_camera_initialized,0);EQ(writes,0);return 0;}
 if(id==2 || id==30 || id==32){
  EQ(result,SYS_ERR_IO);EQ(s_camera_initialized,0);
  if(id!=32)return 0;
  fail_reg=-1;EQ(App_Init(),SYS_OK);
 }
 else EQ(result,SYS_OK);
 EQ(s_camera_initialized,1);EQ(verify_reads,0);
 //生产路径结束后，由隔离测试主动回读17个寄存器
 verified=Test_VerifyRegisters(&report);
 g_result.passed=(uint32_t)verified;g_result.failed=report.checked_count;g_result.done=report.failed_reg;
 if(id>=3 && id<=19){
  EQ(verified,SYS_ERR_PROTOCOL);EQ(report.failed_reg,s_test_verify[id-3].reg);EQ(report.checked_count,id-3);EQ(s_camera_initialized,1);return 0;
 }
 if(id==31){EQ(verified,SYS_ERR_IO);EQ(report.checked_count,0);EQ(s_camera_initialized,1);return 0;}
 EQ(verified,SYS_OK);EQ(report.checked_count,TEST_VERIFY_COUNT);
 EQ(((uint16_t)report.values[0]<<8)|report.values[1],160);
 EQ(((uint16_t)report.values[2]<<8)|report.values[3],120);
 for(i=0;i<TEST_VERIFY_COUNT;i++)EQ(registers[s_test_verify[i].reg],s_test_verify[i].value);
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
