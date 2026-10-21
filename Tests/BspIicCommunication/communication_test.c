/* Compile the unmodified production BSP in this test translation unit.
 * Static state is observed only for assertions; cases initialize via Initcall.
 */
#include <stddef.h>
#include <stdint.h>
#include "../../CM4/BSP/bsp_iic.c"

typedef struct { uint32_t line; int32_t actual, expected; } Check;
typedef struct { uint32_t kind, mask, state; int32_t value; } Trace;
typedef struct {
    uint32_t failure_line, checks, trace_count, delivered, deferred;
    uint32_t sync_calls, async_calls, abort_calls, primask, pending;
    uint32_t state, last_address, abort_address;
    int32_t result;
    uint32_t ev_calls, er_calls, bad_mask_delivery, bad_handle;
} Result;
volatile Result g_result;
Check g_checks[512];
Trace g_trace[256];
static uint32_t tick, mask, pending, event_error, dispatch_kind;
static uint32_t tick_hook, mask_hook, disable_hook, abort_hook;
static uint32_t irq_enable[2], priority[2], subpriority[2];
static uint32_t last_len, sync_timeout;
static uint8_t captured[129], caller_data[130];
static HAL_StatusTypeDef async_return, sync_return, abort_return;
static I2C_HandleTypeDef *last_handle;
static uint32_t mock_source, mock_frequency, mock_mux_failure, mock_init_calls;

/* Trace kinds: 1=start,2=sync,3=abort,4=queued,5=delivered,
 * 6=disable,7=restore,8=tick,9=get-mask,10=EV,11=ER. */
static void TraceAdd(uint32_t kind,int32_t value)
{
    uint32_t n=g_result.trace_count++;
    if(n<256U) { g_trace[n].kind=kind;g_trace[n].mask=mask;
        g_trace[n].state=s_tx_state;g_trace[n].value=value; }
}
static int CheckEq(uint32_t line,int32_t actual,int32_t expected)
{
    uint32_t n=g_result.checks++;
    if(n<512U) {g_checks[n].line=line;g_checks[n].actual=actual;g_checks[n].expected=expected;}
    if(actual!=expected) {if(!g_result.failure_line)g_result.failure_line=line;return 0;}
    return 1;
}
#define EQ(a,b) do { if(!CheckEq(__LINE__,(int32_t)(a),(int32_t)(b)))return 1; } while(0)

void *memcpy(void *dst,const void *src,size_t n)
{uint8_t *d=dst;const uint8_t *s=src;while(n--)*d++=*s++;return dst;}
void *memset(void *dst,int value,size_t n)
{uint8_t *d=dst;while(n--)*d++=(uint8_t)value;return dst;}
/* Volatile loops prevent ARMCC from lowering a runtime stub into itself. */
void __aeabi_memcpy(void *dst,const void *src,size_t n)
{volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memcpy4(void *dst,const void *src,size_t n)
{volatile uint8_t *d=dst;const volatile uint8_t *s=src;while(n--)*d++=*s++;}
void __aeabi_memclr4(void *dst,size_t n)
{volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr8(void *dst,size_t n)
{volatile uint8_t *d=dst;while(n--)*d++=0;}
void __aeabi_memclr(void *dst,size_t n)
{volatile uint8_t *d=dst;while(n--)*d++=0;}

/* Ordinary IRQs are queued while PRIMASK=1 or their NVIC entry is disabled.
 * Events: 1=Tx complete, 2=error, 3=abort complete. The test never invokes
 * production callbacks directly to bypass this queue. */
static void Pump(void)
{
    while(pending && mask==0U) {
        uint32_t kind=pending;
        uint32_t index=kind==2U?1U:0U;
        if(!irq_enable[index])return;
        pending=0U;dispatch_kind=kind;
        g_result.delivered++;TraceAdd(5,(int32_t)kind);
        if(mask)g_result.bad_mask_delivery++;
        if(kind==2U)I2C2_ER_IRQHandler();else I2C2_EV_IRQHandler();
        dispatch_kind=0U;
    }
}
static void Raise(uint32_t kind)
{
    pending=kind;TraceAdd(4,(int32_t)kind);
    if(mask)g_result.deferred++;
    Pump();
}
void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef *h)
{
    g_result.ev_calls++;TraceAdd(10,(int32_t)dispatch_kind);
    if(h!=&hi2c2)g_result.bad_handle++;
    h->State=HAL_I2C_STATE_READY;
    if(dispatch_kind==1U)HAL_I2C_MasterTxCpltCallback(h);
    else if(dispatch_kind==3U)HAL_I2C_AbortCpltCallback(h);
}
void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef *h)
{
    g_result.er_calls++;TraceAdd(11,(int32_t)dispatch_kind);
    if(h!=&hi2c2)g_result.bad_handle++;
    h->State=HAL_I2C_STATE_READY;h->ErrorCode=event_error;
    HAL_I2C_ErrorCallback(h);
}
uint32_t Mock_GetPrimask(void)
{
    uint32_t saved=mask;
    TraceAdd(9,(int32_t)saved);
    if(mask_hook){uint32_t e=mask_hook;mask_hook=0;Raise(e);}
    return saved;
}
void Mock_DisableIrq(void)
{
    /* Model the last legal interrupt boundary before CPSID i. */
    if(disable_hook){uint32_t e=disable_hook;disable_hook=0;Raise(e);}
    mask=1;TraceAdd(6,1);
}
void Mock_SetPrimask(uint32_t value)
{mask=value&1U;TraceAdd(7,(int32_t)mask);Pump();}
uint32_t HAL_GetTick(void)
{
    uint32_t now=tick;TraceAdd(8,(int32_t)now);
    if(tick_hook){uint32_t e=tick_hook;tick_hook=0;Raise(e);}
    return now;
}
HAL_I2C_StateTypeDef HAL_I2C_GetState(I2C_HandleTypeDef *h)
{return (HAL_I2C_StateTypeDef)h->State;}
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *h){return h->ErrorCode;}
HAL_StatusTypeDef HAL_I2C_Master_Transmit_IT(I2C_HandleTypeDef *h,uint16_t addr,uint8_t *data,uint16_t len)
{
    g_result.async_calls++;g_result.last_address=addr;last_handle=h;
    last_len=len;(void)memcpy(captured,data,len);TraceAdd(1,addr);
    if(async_return==HAL_OK)h->State=HAL_I2C_STATE_BUSY_TX;
    return async_return;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h,uint16_t addr,uint8_t *data,uint16_t len,uint32_t timeout)
{
    g_result.sync_calls++;g_result.last_address=addr;last_handle=h;
    last_len=len;sync_timeout=timeout;(void)data;TraceAdd(2,addr);
    return sync_return;
}
HAL_StatusTypeDef HAL_I2C_Master_Abort_IT(I2C_HandleTypeDef *h,uint16_t addr)
{
    g_result.abort_calls++;g_result.abort_address=addr;last_handle=h;TraceAdd(3,addr);
    if(abort_return==HAL_OK)h->State=HAL_I2C_STATE_ABORT;
    /* An IRQ becoming pending during HAL Abort must wait for PRIMASK. */
    if(abort_hook){uint32_t e=abort_hook;abort_hook=0;Raise(e);}
    return abort_return;
}
void Test_GPIOClock(void){}
void Test_I2CClock(unsigned enabled){(void)enabled;}
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c)
{if(!mock_mux_failure)mock_source=c->I2c123ClockSelection;return HAL_OK;}
/* Match the actual HAL's unsupported I2C query; do not hide a regression. */
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t p){(void)p;return 0U;}
uint32_t Mock_I2C2Source(void){return mock_source;}
uint32_t HAL_RCC_GetPCLK1Freq(void){return mock_frequency;}
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h){mock_init_calls++;HAL_I2C_MspInit(h);h->State=HAL_I2C_STATE_READY;return HAL_OK;}
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *h){HAL_I2C_MspDeInit(h);h->State=HAL_I2C_STATE_RESET;return HAL_OK;}
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *h,uint32_t v){(void)h;(void)v;return HAL_OK;}
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *h,uint32_t v){(void)h;(void)v;return HAL_OK;}
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *g){(void)p;(void)g;}
void HAL_GPIO_DeInit(void *p,uint32_t pins){(void)p;(void)pins;}
void HAL_NVIC_SetPriority(IRQn_Type irq,uint32_t p,uint32_t s){priority[irq-33]=p;subpriority[irq-33]=s;}
void HAL_NVIC_EnableIRQ(IRQn_Type irq){irq_enable[irq-33]=1;Pump();}
void HAL_NVIC_DisableIRQ(IRQn_Type irq){irq_enable[irq-33]=0;}
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq)
{if((irq==I2C2_ER_IRQn && pending==2U)||(irq==I2C2_EV_IRQn && pending!=2U))pending=0;}

static int CaseRun(uint32_t id)
{
    SysResult_t value=123;
    uint32_t i, first;
    mock_frequency=120000000U;
    EQ(g_framework_init_BSP_IIC_Init.level,INIT_LEVEL_BUS);
    if(id==29U || id==30U){
        if(id==29U){mock_mux_failure=1;mock_source=1U;}
        else mock_frequency=60000000U;
        EQ(g_framework_init_BSP_IIC_Init.init(),SYS_ERR_INVALID_STATE);
        EQ(mock_init_calls,0);EQ(s_initialized,false);EQ(s_initializing,false);
        EQ(irq_enable[0],0);EQ(irq_enable[1],0);return 0;
    }
    EQ(g_framework_init_BSP_IIC_Init.init(),SYS_OK);
    if(id==31U){
        EQ(g_framework_init_BSP_IIC_Init.init(),SYS_OK);EQ(mock_init_calls,1);
        mock_source=1;EQ(g_framework_init_BSP_IIC_Init.init(),SYS_ERR_INVALID_STATE);
        mock_source=RCC_I2C123CLKSOURCE_D2PCLK1;mock_frequency=60000000U;
        EQ(g_framework_init_BSP_IIC_Init.init(),SYS_ERR_INVALID_STATE);
        mock_frequency=120000000U;EQ(g_framework_init_BSP_IIC_Init.init(),SYS_OK);
        EQ(mock_init_calls,1);return 0;
    }
    for(i=0;i<130U;i++)caller_data[i]=(uint8_t)i;
    EQ(irq_enable[0],1);EQ(irq_enable[1],1);
    EQ(priority[0],0);EQ(priority[1],0);
    EQ(subpriority[0],0);EQ(subpriority[1],0);
    if(id==15U) { /* Invalid pointer/address matrix, including zero length. */
        EQ(BSP_IIC_WriteAsync(0x3C,0,1),SYS_ERR_INVALID_ARG);
        EQ(BSP_IIC_WriteSync(0x3C,0,1),SYS_ERR_INVALID_ARG);
        EQ(BSP_IIC_WriteAsync(0x3C,caller_data,0),SYS_ERR_INVALID_ARG);
        EQ(BSP_IIC_WriteSync(0x3C,caller_data,0),SYS_ERR_INVALID_ARG);
        for(i=0;i<256U;i++)if(i<8U || i>0x77U){
            EQ(BSP_IIC_WriteAsync((uint8_t)i,caller_data,1),SYS_ERR_INVALID_ARG);
            EQ(BSP_IIC_WriteSync((uint8_t)i,caller_data,1),SYS_ERR_INVALID_ARG);
        }
        EQ(g_result.async_calls,0);EQ(g_result.sync_calls,0);return 0;
    }
    if(id==14U){EQ(BSP_IIC_WriteAsync(0x3C,caller_data,130),SYS_ERR_INVALID_ARG);EQ(g_result.async_calls,0);return 0;}
    if(id==18U){
        async_return=HAL_BUSY;EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_ERR_BUSY);
        EQ(s_tx_state,BSP_IIC_IDLE);EQ(BSP_IIC_TakeResult(&value),false);
        async_return=HAL_OK;EQ(BSP_IIC_WriteAsync(0x3D,caller_data,1),SYS_OK);
        Raise(1);EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_OK);return 0;
    }
    if(id==20U){ /* Synchronous call and all HAL return mappings. */
        HAL_StatusTypeDef statuses[4]={HAL_OK,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT};
        int32_t expected[4]={SYS_OK,SYS_ERR_IO,SYS_ERR_BUSY,SYS_ERR_TIMEOUT};
        for(i=0;i<4;i++){sync_return=statuses[i];EQ(BSP_IIC_WriteSync(0x3C,caller_data,2),expected[i]);EQ(s_tx_state,BSP_IIC_IDLE);}
        EQ(g_result.sync_calls,4);EQ(sync_timeout,100);EQ(g_result.last_address,0x78);EQ(last_len,2);
        EQ(last_handle==&hi2c2,1);return 0;
    }
    if(id==21U){ /* HAL state admission checks. */
        hi2c2.State=HAL_I2C_STATE_RESET;
        EQ(BSP_IIC_WriteSync(0x3C,caller_data,1),SYS_ERR_INVALID_STATE);
        EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_ERR_BUSY);
        hi2c2.State=HAL_I2C_STATE_BUSY_TX;
        EQ(BSP_IIC_WriteSync(0x3C,caller_data,1),SYS_ERR_BUSY);
        EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_ERR_BUSY);
        EQ(g_result.sync_calls,0);EQ(g_result.async_calls,0);return 0;
    }
    if(id==22U){ /* NVIC init/deinit symmetric. */
        Mock_SetPrimask(1);Raise(1);EQ(g_result.delivered,0);
        HAL_I2C_MspDeInit(&hi2c2);EQ(irq_enable[0],0);EQ(irq_enable[1],0);EQ(pending,0);
        Mock_SetPrimask(0);EQ(g_result.delivered,0);return 0;
    }
    if(id==23U)tick=0xFFFFFFF0U;
    if(id==11U){
        uint8_t addresses[3]={8,0x3C,0x77};
        for(i=0;i<3;i++){
            EQ(BSP_IIC_WriteAsync(addresses[i],caller_data,1),SYS_OK);
            EQ(g_result.last_address,(uint32_t)addresses[i]<<1);
            Raise(1);EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_OK);
            EQ(BSP_IIC_WriteSync(addresses[i],caller_data,1),SYS_OK);
            EQ(g_result.last_address,(uint32_t)addresses[i]<<1);
        }return 0;
    }
    EQ(BSP_IIC_WriteAsync(0x3C,caller_data,id==13U?129:2),SYS_OK);
    EQ(last_handle==&hi2c2,1);EQ(s_tx_state,BSP_IIC_TRANSFERRING);
    if(id==0U){EQ(g_result.async_calls,1);EQ(g_result.last_address,0x78);EQ(last_len,2);return 0;}
    if(id==1U || id==2U){
        Raise(1);EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_OK);EQ(s_tx_state,BSP_IIC_IDLE);
        if(id==2U){value=123;EQ(BSP_IIC_TakeResult(&value),false);EQ(value,123);}
        return 0;
    }
    if(id==3U){EQ(BSP_IIC_WriteAsync(0x3D,caller_data,2),SYS_ERR_BUSY);EQ(BSP_IIC_WriteSync(0x3D,caller_data,2),SYS_ERR_BUSY);EQ(g_result.async_calls,1);return 0;}
    if(id==9U || id==10U){
        event_error=id==9U?HAL_I2C_ERROR_AF:HAL_I2C_ERROR_TIMEOUT|HAL_I2C_ERROR_AF;
        Raise(2);EQ(BSP_IIC_TakeResult(&value),true);EQ(value,id==9U?SYS_ERR_IO:SYS_ERR_TIMEOUT);
        EQ(g_result.er_calls,1);EQ(BSP_IIC_TakeResult(&value),false);return 0;
    }
    if(id==13U){
        EQ(last_len,129);for(i=0;i<129;i++)EQ(captured[i],caller_data[i]);
        (void)memset(caller_data,0,130);for(i=0;i<129;i++)EQ(s_tx_buffer[i],(uint8_t)i);
        Raise(1);EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_OK);return 0;
    }
    if(id==24U){ /* Pending completion blocked and then delivered. */
        Mock_SetPrimask(1);Raise(1);EQ(g_result.delivered,0);EQ(s_tx_state,BSP_IIC_TRANSFERRING);
        Mock_SetPrimask(0);EQ(g_result.delivered,1);EQ(s_tx_state,BSP_IIC_COMPLETED);
        EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_OK);return 0;
    }
    if(id==25U){
        EQ(BSP_IIC_TakeResult(0),false);EQ(s_tx_state,BSP_IIC_TRANSFERRING);
        Raise(1);EQ(BSP_IIC_TakeResult(0),false);EQ(s_tx_state,BSP_IIC_COMPLETED);
        EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_OK);return 0;
    }
    if(id==26U){
        async_return=HAL_ERROR; /* Finish first transaction then test start errors. */
        Raise(1);EQ(BSP_IIC_TakeResult(&value),true);
        EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_ERR_IO);EQ(s_tx_state,BSP_IIC_IDLE);
        async_return=HAL_TIMEOUT;EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_ERR_TIMEOUT);EQ(s_tx_state,BSP_IIC_IDLE);
        async_return=HAL_OK;EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_OK);return 0;
    }
    if(id==4U || id==23U){
        tick+=99;EQ(BSP_IIC_TakeResult(&value),false);EQ(g_result.abort_calls,0);EQ(s_tx_state,BSP_IIC_TRANSFERRING);
        if(id==4U)return 0;
        tick++;EQ(BSP_IIC_TakeResult(&value),false);EQ(g_result.abort_calls,1);EQ(s_tx_state,BSP_IIC_ABORTING);
        Raise(3);EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_ERR_TIMEOUT);return 0;
    }
    if(id==12U){ /* A previous address must not leak into this abort. */
        Raise(1);EQ(BSP_IIC_TakeResult(&value),true);
        EQ(BSP_IIC_WriteAsync(0x55,caller_data,1),SYS_OK);
    }
    tick+=100U;
    if(id==16U || id==27U || id==28U){
        /* Race boundaries: Tick read, PRIMASK read, immediately before mask. */
        if(id==16U)mask_hook=1;else if(id==27U)tick_hook=1;else disable_hook=1;
        EQ(BSP_IIC_TakeResult(&value),false);EQ(s_tx_state,BSP_IIC_COMPLETED);EQ(g_result.abort_calls,0);
        EQ(mask,0);EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_OK);return 0;
    }
    if(id==7U || id==8U)abort_return=HAL_ERROR;
    if(id==6U)abort_hook=3; /* Queued under PRIMASK, delivered on restore. */
    if(id==17U) {
        Mock_SetPrimask(1);abort_hook=3;
        EQ(BSP_IIC_TakeResult(&value),false);EQ(mask,1);EQ(g_result.delivered,0);EQ(pending,3);
        EQ(s_tx_state,BSP_IIC_ABORTING);Mock_SetPrimask(0);
        EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_ERR_TIMEOUT);EQ(mask,0);
        /* Repeat with initial PRIMASK=0. */
        EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_OK);tick+=100;abort_hook=3;
        EQ(BSP_IIC_TakeResult(&value),false);EQ(mask,0);
        EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_ERR_TIMEOUT);return 0;
    }
    first=BSP_IIC_TakeResult(&value);
    EQ(g_result.abort_calls,1);EQ(g_result.abort_address,id==12U?0xAA:0x78);EQ(mask,0);
    if(id==7U || id==8U){
        EQ(first,true);EQ(value,SYS_ERR_IO);EQ(s_tx_state,BSP_IIC_FAULT);
        EQ(BSP_IIC_TakeResult(&value),false);
        if(id==8U){EQ(BSP_IIC_WriteSync(0x3C,caller_data,1),SYS_ERR_IO);EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_ERR_IO);}
        return 0;
    }
    EQ(first,false);
    if(id==6U){
        EQ(g_result.deferred,1);EQ(g_result.delivered,1);EQ(s_tx_state,BSP_IIC_COMPLETED);
        EQ(BSP_IIC_TakeResult(&value),true);EQ(value,SYS_ERR_TIMEOUT);
        EQ(BSP_IIC_TakeResult(&value),false);return 0;
    }
    EQ(s_tx_state,BSP_IIC_ABORTING);
    if(id==19U){
        for(i=0;i<8;i++){tick+=1000;EQ(BSP_IIC_TakeResult(&value),false);EQ(s_tx_state,BSP_IIC_ABORTING);}
        EQ(g_result.abort_calls,1);EQ(BSP_IIC_WriteAsync(0x3C,caller_data,1),SYS_ERR_BUSY);
        EQ(BSP_IIC_WriteSync(0x3C,caller_data,1),SYS_ERR_BUSY);return 0;
    }
    /* T06 / T13 stop after checking abort request and its current address. */
    return 0;
}

uint32_t Communication_TestRun(uint32_t id)
{
    uint32_t failed=(uint32_t)CaseRun(id);
    g_result.primask=mask;g_result.pending=pending;g_result.state=s_tx_state;
    g_result.result=s_tx_result;
    if(g_result.bad_mask_delivery || g_result.bad_handle)failed=1;
    return failed;
}
