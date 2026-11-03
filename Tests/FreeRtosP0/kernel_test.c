#include "../../CM7/Platform/platform_rtos.c"
#include "../../CM7/Platform/platform_hal_timebase.c"
#include <string.h>

uint32_t SystemCoreClock = 480000000U;
uint32_t SystemD2Clock = 240000000U;
uint32_t uwTickPrio;
static uint32_t test_hclk=240000000U,test_pclk=120000000U,test_hal_tick;
uint32_t HAL_RCC_GetHCLKFreq(void){return test_hclk;}
uint32_t HAL_RCC_GetPCLK1Freq(void){return test_pclk;}
void HAL_IncTick(void){test_hal_tick++;}
void *memset(void *d,int v,size_t n){uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
void *memcpy(void *d,const void *s,size_t n){uint8_t *p=d;const uint8_t *q=s;while(n--)*p++=*q++;return d;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
void __aeabi_memset(void *d,size_t n,int v){volatile uint8_t *p=d;while(n--)*p++=(uint8_t)v;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}

volatile uint32_t g_result[4];
static StaticTask_t tcb_a,tcb_b;
static StackType_t stack_a[256],stack_b[256];
static void Dummy(void *p){(void)p;for(;;){}}
static void DummyRun(void){}
#define EQ(a,b) do{g_result[1]++;if((uint32_t)(a)!=(uint32_t)(b)){g_result[0]=__LINE__;g_result[2]=(uint32_t)(a);g_result[3]=(uint32_t)(b);return 1;}}while(0)
extern void vTaskSwitchContext(void);
extern BaseType_t xTaskIncrementTick(void);
extern void xPortSysTickHandler(void);

int Kernel_TestRun(uint32_t id)
{
    TaskHandle_t a,b;
    StaticTask_t *idle;
    StackType_t *stack;
    uint32_t size,i;
    if(id==0){EQ(Platform_RtosStart(NULL),SYS_ERR_INVALID_ARG);EQ(uxTaskGetNumberOfTasks(),0);return 0;}
    if(id==1){__disable_irq();EQ(Platform_RtosStart(DummyRun),SYS_ERR_INVALID_STATE);__enable_irq();EQ(uxTaskGetNumberOfTasks(),0);return 0;}
    if(id==2){EQ(Platform_RtosStart(DummyRun),SYS_ERR_INVALID_STATE);EQ(uxTaskGetNumberOfTasks(),2);EQ(xTaskGetSchedulerState(),taskSCHEDULER_RUNNING);EQ(Platform_RtosStart(DummyRun),SYS_ERR_INVALID_STATE);EQ(uxTaskGetNumberOfTasks(),2);return 0;}
    if(id==3){vApplicationGetIdleTaskMemory(&idle,&stack,&size);EQ(idle,&s_idle_tcb);EQ(stack,s_idle_stack);EQ(size,256);EQ((uint32_t)stack&7,0);return 0;}
    if(id==8){
        EQ(HalTimebase_TimerClock(240000000,120000000,0),240000000);
        EQ(HalTimebase_TimerClock(240000000,240000000,0),240000000);
        EQ(HalTimebase_TimerClock(240000000,60000000,1),240000000);
        EQ(HalTimebase_TimerClock(240000000,30000000,1),120000000);
        EQ(HalTimebase_TimerClock(240000000,30000000,0),60000000);
        EQ(HalTimebase_TimerClock(240000000,0,0),0);
        EQ(HalTimebase_TimerClock(1,2,0),0);return 0;
    }
    if(id==9){
        RCC->CFGR=0;EQ(HAL_InitTick(15),HAL_OK);EQ(TIM6->PSC,239);EQ(TIM6->ARR,999);EQ(uwTickPrio,4);
        EQ(NVIC_GetPriority(TIM6_DAC_IRQn),4);EQ(NVIC_GetEnableIRQ(TIM6_DAC_IRQn),1);
        TIM6->SR=TIM_SR_UIF;TIM6_DAC_IRQHandler();EQ(test_hal_tick,1);
        HAL_SuspendTick();TIM6->SR=TIM_SR_UIF;TIM6_DAC_IRQHandler();EQ(test_hal_tick,1);
        HAL_ResumeTick();TIM6->SR=TIM_SR_UIF;TIM6_DAC_IRQHandler();EQ(test_hal_tick,2);
        test_hclk=64000000;test_pclk=64000000;EQ(HAL_InitTick(15),HAL_OK);EQ(TIM6->PSC,63);
        EQ(HAL_InitTick(16),HAL_ERROR);return 0;
    }
    a=xTaskCreateStatic(Dummy,"A",256,NULL,3,stack_a,&tcb_a);
    b=xTaskCreateStatic(Dummy,"B",256,NULL,2,stack_b,&tcb_b);
    EQ(a!=NULL,1);EQ(b!=NULL,1);EQ(uxTaskGetNumberOfTasks(),2);
    vTaskStartScheduler();EQ(xTaskGetSchedulerState(),taskSCHEDULER_RUNNING);EQ(uxTaskGetNumberOfTasks(),3);
    EQ(xTaskGetCurrentTaskHandle(),a);
    if(id==4){
        vTaskDelay(2);vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),b);
        vTaskDelay(1);vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),xTaskGetIdleTaskHandle());
        xTaskIncrementTick();vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),b);
        xTaskIncrementTick();vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),a);EQ(xTaskGetTickCount(),2);return 0;
    }
    if(id==5){
        vTaskSuspendAll();xTaskIncrementTick();xTaskIncrementTick();EQ(xTaskGetTickCount(),0);xTaskResumeAll();EQ(xTaskGetTickCount(),2);return 0;
    }
    if(id==6){
        EQ(uxTaskGetStackHighWaterMark(a)>128,1);EQ(uxTaskGetStackHighWaterMark(b)>128,1);
        // Port保存r4-r11及EXC_RETURN共9个Word，恢复后硬件帧必须8字节对齐
        EQ((uint32_t)((*(StackType_t **)a)+9)&7,0);EQ((uint32_t)((*(StackType_t **)b)+9)&7,0);
        vTaskSuspend(a);vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),b);vTaskResume(a);vTaskSwitchContext();EQ(xTaskGetCurrentTaskHandle(),a);return 0;
    }
    if(id==7){
        xPortSysTickHandler();EQ(xTaskGetTickCount(),1);EQ(g_rtos_debug.tick_count,1);EQ(test_hal_tick,0);
        TIM6->DIER=TIM_DIER_UIE;TIM6->SR=TIM_SR_UIF;TIM6_DAC_IRQHandler();EQ(test_hal_tick,1);EQ(xTaskGetTickCount(),1);
        for(i=0;i<999;i++)xPortSysTickHandler();EQ(xTaskGetTickCount(),1000);EQ(g_rtos_debug.tick_count,1000);return 0;
    }
    if(id==10){stack_a[0]=0;vTaskSwitchContext();return 1;}
    return 1;
}
