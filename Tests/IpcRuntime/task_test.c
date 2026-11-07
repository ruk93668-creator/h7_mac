#include "stm32h7xx.h"
//只控制执行环境观察值，通知和任务控制块使用真实FreeRTOS内核
static uint32_t test_ipsr;
#define __get_IPSR() test_ipsr
#include "../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c"
#include "../../CM7/Platform/platform_rtos.c"
#include "../../Common/Src/runtime_event.c"
#include "../../CM7/Application/app_events.c"
#include "../../CM7/Application/Tasks/ipc_task.c"
#include "../../CM7/Application/app_tasks.c"
#include <string.h>

uint32_t SystemCoreClock=480000000U;
volatile uint32_t g_case,g_fail_line,g_runtime_starts,g_runtime_steps,g_authorizations;
static uint32_t s_tick;
static bool (*s_owner_check)(void *);
static void *s_owner_context;
static StaticTask_t s_other_tcbs[3];
static StackType_t s_other_stacks[3][128];
static uint32_t s_other_count;
#define CHECK(x) do{if(!(x)){g_fail_line=__LINE__;return 1;}}while(0)

void *memset(void *d,int v,size_t n){volatile uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){__aeabi_memcpy(d,s,n);}
void __aeabi_memset(void *d,size_t n,int v){volatile uint8_t *p=d;while(n--)*p++=(uint8_t)v;}
uint32_t Platform_TimeGetMs(void){return s_tick;}
SysResult_t App_MessagesInit(void){return SYS_OK;}
SysResult_t USB_Device_SetEventCallback(UsbDeviceEventCallback_t cb,void *context){(void)cb;(void)context;return SYS_OK;}
SysResult_t CameraCapture_SetEventCallback(CameraCaptureEventCallback_t cb,void *context){(void)cb;(void)context;return SYS_OK;}
SysResult_t BSP_DCMI_SetEventCallback(BspDcmiEventCallback_t cb,void *context){(void)cb;(void)context;return SYS_OK;}

//其他业务仅创建静态接收端，不运行Camera/USB业务
static void Dummy(void *p){(void)p;for(;;){}}
static SysResult_t OtherCreate(RuntimeEventTarget_t target,UBaseType_t priority)
{
    uint32_t i=s_other_count++;
    TaskHandle_t t=xTaskCreateStatic(Dummy,"Other",128U,NULL,priority,s_other_stacks[i],&s_other_tcbs[i]);
    return t==NULL?SYS_ERR_NO_RESOURCE:App_EventsAttach(target,t);
}
SysResult_t CameraTask_Create(void){return OtherCreate(RUNTIME_TARGET_CAMERA,3U);}
SysResult_t UsbTask_Create(void){return OtherCreate(RUNTIME_TARGET_USB,4U);}
SysResult_t ControlTask_Create(void){return OtherCreate(RUNTIME_TARGET_CONTROL,2U);}

//只替换Service边界，验证实际IPC任务的调用顺序和当前任务身份
SysResult_t IPC_Service_SetRuntimeOwner(bool (*check)(void *),void *context)
{
    if(g_case==4U)return SYS_ERR_IO;
    s_owner_check=check;s_owner_context=context;g_authorizations++;return SYS_OK;
}
SysResult_t IPC_Service_RuntimeStart(uint32_t now)
{
    (void)now;
    if(!s_boot_ready || !s_owner_check(s_owner_context)){g_fail_line=__LINE__;return SYS_ERR_INVALID_STATE;}
    g_runtime_starts++;return SYS_OK;
}
SysResult_t IPC_Service_RuntimeProcess(uint32_t now)
{
    (void)now;
    if(!s_owner_check(s_owner_context)){g_fail_line=__LINE__;return SYS_ERR_INVALID_STATE;}
    g_runtime_steps++;return SYS_OK;
}

//建立真实内核通知的运行环境，不模拟SVC或PendSV异常返回
static void Running(void)
{
    xSchedulerRunning=pdTRUE;
    __set_BASEPRI(0U);
    pxCurrentTCB=s_tasks[RUNTIME_TARGET_CONTROL];
}

int IpcTask_TestRun(uint32_t id)
{
    uint32_t events,i;
    SysResult_t result;
    g_case=id;
    if(id==0U){CHECK(RUNTIME_TARGET_COUNT==4U);CHECK(sizeof(s_tasks)/sizeof(s_tasks[0])==4U);return 0;}
    if(id==1U){test_ipsr=16U;CHECK(IpcTask_Create()==SYS_ERR_INVALID_STATE);return 0;}
    result=App_TasksCreate();
    if(id==3U || id==4U || id==5U)
    {
        CHECK(result==(id==3U?SYS_ERR_NO_RESOURCE:SYS_ERR_IO));
        CHECK(g_app_tasks_debug.created_tasks==3U && !s_prepared && !s_boot_ready);
        CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_ERR_INVALID_STATE);
        CHECK(g_runtime_starts==0U);return 0;
    }
    CHECK(result==SYS_OK);
    CHECK(g_app_tasks_debug.created_tasks==4U && uxTaskGetNumberOfTasks()==4U);
    CHECK(s_tasks[RUNTIME_TARGET_IPC]==s_ipc_task && g_authorizations==1U);
    CHECK(g_runtime_starts==0U && g_runtime_steps==0U);
    if(id==2U){CHECK(IpcTask_Create()==SYS_ERR_ALREADY_EXISTS);return 0;}
    if(id==6U){CHECK(App_TasksRecordInitialization(SYS_ERR_IO)==SYS_ERR_IO);CHECK(!s_prepared && !s_boot_ready);return 0;}
    if(id==7U){s_tasks[RUNTIME_TARGET_IPC]=NULL;CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==8U)CHECK(RuntimeEvent_Post(RUNTIME_TARGET_IPC,RUNTIME_EVENT_WAKE)==SYS_OK);
    CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_OK);
    Running();
    if(id==9U){pxCurrentTCB=s_ipc_task;CHECK(App_TasksReleaseBoot()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==10U)
    {
        pxCurrentTCB=s_ipc_task;
        //测试运行器在首次无限等待处停止，确认未提前推进Service
        IpcTask_Run(NULL);return 1;
    }
    CHECK(App_TasksReleaseBoot()==SYS_OK);
    if(id==11U){CHECK(App_TasksReleaseBoot()==SYS_ERR_ALREADY_EXISTS);return 0;}
    if(id==12U){CHECK(IpcTask_Create()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==13U)
    {
        for(i=0U;i<RUNTIME_TARGET_COUNT;i++)
        {
            pxCurrentTCB=s_tasks[i];CHECK(App_EventsWait((RuntimeEventTarget_t)i,0U,&events)==SYS_OK);
            CHECK((events&RUNTIME_EVENT_BOOT_READY)!=0U);
        }
        return 0;
    }
    if(id==14U)
    {
        pxCurrentTCB=s_tasks[RUNTIME_TARGET_CAMERA];CHECK(!s_owner_check(s_owner_context));
        pxCurrentTCB=s_tasks[RUNTIME_TARGET_USB];CHECK(!s_owner_check(s_owner_context));
        pxCurrentTCB=s_tasks[RUNTIME_TARGET_CONTROL];CHECK(!s_owner_check(s_owner_context));
        pxCurrentTCB=s_ipc_task;CHECK(s_owner_check(s_owner_context));
        __disable_irq();CHECK(!s_owner_check(s_owner_context));__enable_irq();return 0;
    }
    pxCurrentTCB=s_ipc_task;
    if(id==8U){CHECK(App_EventsWait(RUNTIME_TARGET_IPC,0U,&events)==SYS_OK);CHECK(events==(RUNTIME_EVENT_BOOT_READY|RUNTIME_EVENT_WAKE));return 0;}
    if(id==15U){CHECK(sizeof(s_ipc_stack)==2048U && ((uintptr_t)s_ipc_stack&7U)==0U);return 0;}
    if(id==16U)
    {
        //测试运行器在实际vTaskDelay入口停止，检查100ms阻塞参数
        IpcTask_Run(NULL);return 1;
    }
    if(id==17U){test_ipsr=16U;CHECK(!s_owner_check(s_owner_context));return 0;}
    if(id==18U){CHECK(App_EventsWait(RUNTIME_TARGET_USB,0U,&events)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==19U){CHECK(App_TasksCreate()==SYS_ERR_INVALID_STATE);return 0;}
    return 1;
}

//05C-2新增依赖桩；原Task/BOOT断言保持不变
#if !defined(IPC_STATUS_REAL_APP)
SysResult_t App_StatusGetSnapshot(IpcApplicationStatus_t *snapshot)
{
    memset(snapshot,0,sizeof(*snapshot));return SYS_OK;
}
SysResult_t App_StatusGetSnapshotAt(IpcApplicationStatus_t *snapshot, uint32_t *tick)
{
    *tick=0U;return App_StatusGetSnapshot(snapshot);
}
#endif
SysResult_t IPC_Service_SetApplication(const IpcApplicationStatus_t *application,uint32_t sampled_tick)
{
    (void)application;(void)sampled_tick;return s_owner_check(s_owner_context)?SYS_OK:SYS_ERR_INVALID_STATE;
}
