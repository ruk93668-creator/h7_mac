/* Directly compile current production OLED logic; only BSP is mocked. */
#include "../../CM4/Device/Src/oled_device.c"

typedef struct { uint32_t before[4]; OledDevice_t oled; uint32_t after[4]; } Guarded;
Guarded g_guarded;
typedef struct { uint32_t len,addr; int32_t result; uint8_t bytes[129]; } Transaction;
Transaction g_tx[32];
typedef struct {uint32_t kind,state,page,tx_count; int32_t result;} Step;
Step g_steps[256];
typedef struct {
 uint32_t fail_line,checks,tx_count,attempts,takes,active,ready,overlap;
 uint32_t step_count,state,page,frame_offset,oled_size,frame_size,tx_size;
 int32_t last_result;
} TestResult;
volatile TestResult g_result;
static SysResult_t completion_result,inject_result;
static uint32_t inject_attempt;
static uint8_t frame_salt;

void *memcpy(void *dst,const void *src,size_t n)
{uint8_t *d=dst;const uint8_t *s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int v,size_t n)
{uint8_t *d=dst;while(n--)*d++=(uint8_t)v;return dst;}
void __aeabi_memcpy(void *dst,const void *src,size_t n)
{volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memcpy4(void *dst,const void *src,size_t n)
{volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memclr(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr4(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr8(void *dst,size_t n){volatile uint8_t *d=dst;while(n--)*d++=0;}

static int Check(uint32_t line,int32_t a,int32_t b)
{g_result.checks++;if(a!=b){if(!g_result.fail_line)g_result.fail_line=line;return 0;}return 1;}
#define EQ(a,b) do{if(!Check(__LINE__,(int32_t)(a),(int32_t)(b)))return 1;}while(0)
static void Record(uint32_t kind,SysResult_t r)
{
 uint32_t n=g_result.step_count++;
 if(n<256){g_steps[n].kind=kind;g_steps[n].state=g_guarded.oled.refresh_state;
  g_steps[n].page=g_guarded.oled.refresh_page;g_steps[n].tx_count=g_result.tx_count;g_steps[n].result=r;}
 g_result.last_result=r;
}
SysResult_t BSP_IIC_WriteSync(uint8_t addr,uint8_t *data,uint16_t len)
{(void)addr;(void)data;(void)len;return SYS_OK;}
SysResult_t BSP_IIC_WriteAsync(uint8_t addr,const uint8_t *data,uint16_t len)
{
 uint32_t n=g_result.attempts++;
 Transaction *tx;
 if(g_result.active || g_result.ready){g_result.overlap++;return SYS_ERR_BUSY;}
 if(n==inject_attempt)return inject_result;
 if(!data || !len || len>129U || g_result.tx_count>=32U)return SYS_ERR_INVALID_ARG;
 tx=&g_tx[g_result.tx_count++];tx->len=len;tx->addr=addr;tx->result=SYS_OK;
 /* Production BSP copies input before returning: local OLED buffers are valid. */
 (void)memcpy(tx->bytes,data,len);g_result.active=1;Record(3,SYS_OK);return SYS_OK;
}
bool BSP_IIC_TakeResult(SysResult_t *r)
{
 g_result.takes++;
 if(!g_result.ready)return false;
 *r=completion_result;g_result.ready=0;Record(4,*r);return true;
}
static void Complete(SysResult_t result)
{
 if(!g_result.active){g_result.fail_line=__LINE__;return;}
 g_result.active=0;g_result.ready=1;completion_result=result;Record(5,result);
}
static SysResult_t Refresh(void)
{SysResult_t r=g_guarded.oled.ops->refresh(&g_guarded.oled);Record(1,r);return r;}
static SysResult_t Process(void)
{SysResult_t r=g_guarded.oled.ops->process(&g_guarded.oled);Record(2,r);return r;}
static uint8_t Pattern(uint32_t index)
{return (uint8_t)((index*29U)^(index>>7U)^frame_salt);}
static void Fill(uint8_t salt)
{uint32_t i;frame_salt=salt;for(i=0;i<OLED_BUFFER_SIZE;i++)g_guarded.oled.framebuffer[i]=Pattern(i);}
static int VerifyTx(uint32_t index,uint32_t stage)
{
 uint32_t i,page=stage/2U;
 Transaction *tx=&g_tx[index];
 EQ(tx->addr,0x3C);EQ(page<4U,1);
 if((stage&1U)==0){
  EQ(tx->len,4);EQ(tx->bytes[0],0);EQ(tx->bytes[1],0xB0U|page);
  EQ(tx->bytes[2],0);EQ(tx->bytes[3],0x10);
 }else{
  EQ(tx->len,129);EQ(tx->bytes[0],0x40);
  for(i=0;i<128;i++)EQ(tx->bytes[i+1],Pattern(page*128+i));
 }
 return 0;
}
static int RunFrame(void)
{
 uint32_t stage,base=g_result.tx_count;
 EQ(Refresh(),SYS_OK);EQ(g_result.tx_count,base+1);
 for(stage=0;stage<8;stage++){
  EQ(g_guarded.oled.refresh_page,stage/2U);
  EQ(g_guarded.oled.refresh_state,(stage&1U)?OLED_REFRESH_WAIT_DATA:OLED_REFRESH_WAIT_CMD);
  EQ(VerifyTx(base+stage,stage),0);
  Complete(SYS_OK);EQ(Process(),SYS_OK);
  EQ(g_result.tx_count,base+(stage<7?stage+2U:8U));
 }
 EQ(g_guarded.oled.refresh_state,OLED_REFRESH_IDLE);EQ(g_guarded.oled.refresh_page,3);
 EQ(g_result.active,0);EQ(g_result.ready,0);EQ(g_result.overlap,0);return 0;
}
static int AdvanceTo(uint32_t stage)
{
 uint32_t i;EQ(Refresh(),SYS_OK);
 for(i=0;i<stage;i++){EQ(VerifyTx(i,i),0);Complete(SYS_OK);EQ(Process(),SYS_OK);}
 EQ(g_result.tx_count,stage+1);EQ(g_guarded.oled.refresh_page,stage/2U);return 0;
}
static int Cases(uint32_t id)
{
 uint32_t i,n,state,page,takes,stage;
 SysResult_t error;
 OledDevice_t *oled=&g_guarded.oled;
 inject_attempt=0xFFFFFFFFU;
 oled->ops=&s_oled_ops;oled->i2c_addr=0x3C;Fill(0x35);
 EQ(g_device_export_s_oled.device==&s_oled.base,1);
 if(id==0U || id==6U){
  EQ(RunFrame(),0);
  if(id==6U){Fill(0xA6);EQ(RunFrame(),0);EQ(g_result.tx_count,16);}
  return 0;
 }
 if(id==1U || id==2U){
  EQ(AdvanceTo(id==1U?0:1),0);state=oled->refresh_state;page=oled->refresh_page;n=g_result.tx_count;
  for(i=0;i<12;i++){EQ(Process(),SYS_OK);EQ(oled->refresh_state,state);EQ(oled->refresh_page,page);EQ(g_result.tx_count,n);}
  EQ(g_result.active,1);return 0;
 }
 if(id==3U || id==4U){
  EQ(AdvanceTo(id==3U?0:1),0);state=oled->refresh_state;page=oled->refresh_page;n=g_result.attempts;
  EQ(Refresh(),SYS_ERR_BUSY);EQ(oled->refresh_state,state);EQ(oled->refresh_page,page);
  EQ(g_result.attempts,n);return 0;
 }
 if(id==5U){
  EQ(AdvanceTo(7),0);EQ(VerifyTx(7,7),0);Complete(SYS_OK);EQ(Process(),SYS_OK);
  EQ(oled->refresh_state,OLED_REFRESH_IDLE);EQ(oled->refresh_page,3);EQ(g_result.tx_count,8);
  takes=g_result.takes;
  for(i=0;i<10;i++)EQ(Process(),SYS_OK);
  EQ(g_result.takes,takes);EQ(g_result.tx_count,8);return 0;
 }
 /* Completion IO/TIMEOUT failures at every CMD/DATA stage. */
 if(id>=7U && id<23U){
  stage=(id-7U)%8U;error=id<15U?SYS_ERR_IO:SYS_ERR_TIMEOUT;
  EQ(AdvanceTo(stage),0);n=g_result.tx_count;Complete(error);EQ(Process(),error);
  EQ(oled->refresh_state,OLED_REFRESH_IDLE);EQ(g_result.tx_count,n);
  EQ(Process(),SYS_OK);EQ(g_result.tx_count,n);return 0;
 }
 /* Submission BUSY/IO/TIMEOUT failures at every transaction position. */
 if(id>=23U && id<47U){
  uint32_t group=(id-23U)/8U;stage=(id-23U)%8U;
  error=group==0U?SYS_ERR_BUSY:group==1U?SYS_ERR_IO:SYS_ERR_TIMEOUT;
  inject_attempt=stage;inject_result=error;
  if(!stage){EQ(Refresh(),error);EQ(g_result.tx_count,0);}
  else{
   EQ(AdvanceTo(stage-1U),0);Complete(SYS_OK);EQ(Process(),error);EQ(g_result.tx_count,stage);
  }
  EQ(oled->refresh_state,OLED_REFRESH_IDLE);EQ(g_result.active,0);
  EQ(Process(),SYS_OK);return 0;
 }
 if(id==47U){
  EQ(Oled_RefreshImpl(NULL),SYS_ERR_INVALID_ARG);EQ(Oled_ProcessImpl(NULL),SYS_ERR_INVALID_ARG);
  EQ(g_result.attempts,0);EQ(g_result.takes,0);return 0;
 }
 if(id==48U){
  oled->refresh_state=(OledRefreshState_t)99;
  EQ(Process(),SYS_ERR_INVALID_STATE);EQ(g_result.attempts,0);EQ(g_result.takes,0);return 0;
 }
 if(id==49U){ /* Successful restart after a completed IO failure. */
  EQ(AdvanceTo(3),0);Complete(SYS_ERR_IO);EQ(Process(),SYS_ERR_IO);
  Fill(0xC1);EQ(RunFrame(),0);return 0;
 }
 return 1;
}
uint32_t Oled_TestRun(uint32_t id)
{
 uint32_t failed;
 g_result.frame_offset=(uint32_t)((uint8_t *)g_guarded.oled.framebuffer-(uint8_t *)&g_guarded);
 g_result.oled_size=sizeof(OledDevice_t);g_result.frame_size=OLED_BUFFER_SIZE;g_result.tx_size=sizeof(Transaction);
 failed=(uint32_t)Cases(id);g_result.state=g_guarded.oled.refresh_state;g_result.page=g_guarded.oled.refresh_page;
 return failed;
}
