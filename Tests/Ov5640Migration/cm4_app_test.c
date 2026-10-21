/* Current CM4 baseline clears the framebuffer without drawing a line. */
/* Actual CM4 Initcall, Device, OLED, Application and runtime/Timer/Event. */
#include "../../CM4/Device/Src/device.c"
#include "../../CM4/Device/Src/oled_device.c"
#include "../../CM4/Application/app_oled.c"
#include "../../CM4/Application/app_main.c"
#include "../../CM4/Framework/framework_timer.c"
#include "../../CM4/Framework/framework_event.c"
#include "../../CM4/Framework/framework_runtime.c"
#include "../../CM4/Framework/framework_init.c"

/* Same two calls as the production CM4 Main_Run USER CODE block. */
static void MainStep(void){Framework_Run();App_Run();}

typedef struct {uint32_t len;uint8_t bytes[129];} Tx;
Tx g_tx[40];
typedef struct {
 uint32_t fail_line,checks,tick,tx_count,takes,active,ready,heap,pool,page,state;
 int32_t refresh_result,process_result,init_result;
} TestResult;
volatile TestResult g_result;
static uint32_t now,active,ready;
static SysResult_t completed;
static SysResult_t forced_submit;
static uint32_t tx_count,take_count;

void *memcpy(void *dst,const void *src,size_t n){uint8_t *d=dst;const uint8_t *s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int v,size_t n){uint8_t *d=dst;while(n--)*d++=(uint8_t)v;return dst;}
void __aeabi_memcpy(void *dst,const void *src,size_t n){volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memcpy4(void *dst,const void *src,size_t n){volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memclr(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr4(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr8(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
uint32_t Platform_TimeGetMs(void){return now;}
SysResult_t BSP_IIC_WriteSync(uint8_t a,uint8_t *data,uint16_t len){(void)a;(void)data;(void)len;return SYS_OK;}
SysResult_t BSP_IIC_WriteAsync(uint8_t a,const uint8_t *data,uint16_t len)
{
 if(forced_submit!=SYS_OK)return forced_submit;
 if(active || ready)return SYS_ERR_BUSY;
 if(a!=0x3C || len>129 || tx_count>=40)return SYS_ERR_INVALID_ARG;
 g_tx[tx_count].len=len;(void)memcpy(g_tx[tx_count].bytes,data,len);tx_count++;active=1;return SYS_OK;
}
bool BSP_IIC_TakeResult(SysResult_t *r)
{take_count++;if(!ready)return false;*r=completed;ready=0;return true;}
static int Check(uint32_t line,int32_t a,int32_t b)
{g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Check(__LINE__,(int32_t)(a),(int32_t)(b)))return 1;}while(0)
static void Complete(SysResult_t result){if(!active){g_result.fail_line=__LINE__;return;}active=0;ready=1;completed=result;}
static void Poll(uint32_t tick){now=tick;MainStep();}
static int Init(void)
{
 g_result.init_result=Framework_InitRuntime();EQ(g_result.init_result,SYS_OK);
 EQ(s_oled.base.state,DEVICE_STATE_READY);EQ(s_app_oled==&s_oled,1);
 EQ(s_timer_pool_count,1);EQ(s_heap_size,1);
 EQ(s_timer_pool[0].config.period_ms,100);EQ(s_timer_pool[0].config.mode,FRAMEWORK_TIMER_PERIODIC);
 EQ(s_timer_pool[0].config.context==&s_oled,1);
 EQ(g_framework_init_App_Init.level,INIT_LEVEL_APPLICATION);return 0;
}
static int FinishFrame(void)
{
 uint32_t base=tx_count-1U,stage,i;
 for(stage=0;stage<8;stage++){
  Tx *tx=&g_tx[base+stage];
  EQ(active,1);EQ(s_oled.refresh_page,stage/2);
  if(!(stage&1U)){EQ(tx->len,4);EQ(tx->bytes[0],0);EQ(tx->bytes[1],0xB0U|(stage/2));EQ(tx->bytes[2],0);EQ(tx->bytes[3],0x10);}
  else{EQ(tx->len,129);EQ(tx->bytes[0],0x40);for(i=0;i<128;i++)EQ(tx->bytes[i+1],s_oled.framebuffer[stage/2*128+i]);}
  Complete(SYS_OK);MainStep();EQ(s_oled_error,SYS_OK);
 }
 EQ(s_oled.refresh_state,OLED_REFRESH_IDLE);EQ(tx_count,base+8);return 0;
}
static void Noop(void *context){(void)context;}
static int Case(uint32_t id)
{
 uint32_t i,n;
 if(id==2){App_Run();EQ(tx_count,0);EQ(take_count,0);EQ(s_app_oled==NULL,1);return 0;}
 if(id==6){
  FrameworkTimerConfig_t config={100,FRAMEWORK_TIMER_PERIODIC,Noop,NULL};FrameworkTimerHandle_t h;
  for(i=0;i<16;i++)EQ(Framework_TimerCreate(&config,&h),SYS_OK);
  EQ(Framework_InitRuntime(),SYS_ERR_NO_RESOURCE);EQ(s_app_oled==NULL,1);EQ(s_timer_pool_count,16);
  App_Run();EQ(tx_count,0);return 0;
 }
 if(id==7){
  EQ(Device_RegisterAll(),SYS_OK);EQ(App_Init(),SYS_ERR_INVALID_STATE);EQ(s_timer_pool_count,0);
  EQ(Device_InitAll(),SYS_OK);EQ(App_Init(),SYS_OK);return 0;
 }
 if(id==8){EQ(App_Init(),SYS_ERR_NOT_FOUND);EQ(s_timer_pool_count,0);return 0;}
 if(id==9){s_oled.ops=NULL;EQ(Framework_InitRuntime(),SYS_ERR_NOT_SUPPORTED);EQ(s_timer_pool_count,0);EQ(s_app_oled==NULL,1);return 0;}
 EQ(Init(),0);
 if(id==0){
  for(i=0;i<OLED_BUFFER_SIZE;i++)EQ(s_oled.framebuffer[i],0U);
  EQ(tx_count,0);return 0;
 }
 if(id==1){FrameworkTimerHandle_t h=s_oled_timer;EQ(App_Init(),SYS_OK);EQ(s_oled_timer,h);EQ(s_timer_pool_count,1);EQ(s_heap_size,1);return 0;}
 Poll(99);EQ(tx_count,0);Poll(100);EQ(tx_count,1);EQ(s_oled_error,SYS_OK);
 if(id==3 || id==10){
  EQ(FinishFrame(),0);Poll(199);EQ(tx_count,8);Poll(200);EQ(tx_count,9);
  EQ(FinishFrame(),0);EQ(tx_count,16);EQ(s_timer_pool_count,1);return 0;
 }
 if(id==4){
  Poll(200);EQ(tx_count,1);EQ(s_oled_error,SYS_OK);EQ(s_oled.refresh_state,OLED_REFRESH_WAIT_CMD);
  Poll(300);EQ(tx_count,1);EQ(s_oled_error,SYS_OK);EQ(s_heap_size,1);
  EQ(FinishFrame(),0);Poll(400);EQ(tx_count,9);return 0;
 }
 if(id==5){
  Complete(SYS_ERR_IO);MainStep();EQ(s_oled_error,SYS_ERR_IO);EQ(s_oled.refresh_state,OLED_REFRESH_IDLE);
  Poll(200);EQ(tx_count,2);EQ(s_oled_error,SYS_ERR_IO);return 0;
 }
 if(id==11){
  n=take_count;for(i=0;i<20;i++)App_Run();EQ(take_count,n+20);EQ(s_oled.refresh_state,OLED_REFRESH_WAIT_CMD);
  Complete(SYS_OK);App_Run();EQ(s_oled.refresh_state,OLED_REFRESH_WAIT_DATA);EQ(tx_count,2);return 0;
 }
 return 1;
}
uint32_t App_TestRun(uint32_t id)
{
 uint32_t r=(uint32_t)Case(id);g_result.tick=now;g_result.tx_count=tx_count;
 g_result.takes=take_count;g_result.active=active;g_result.ready=ready;
 g_result.heap=s_heap_size;g_result.pool=s_timer_pool_count;g_result.page=s_oled.refresh_page;
 g_result.state=s_oled.refresh_state;g_result.refresh_result=s_oled_error;
 g_result.process_result=s_oled_error;return r;
}
