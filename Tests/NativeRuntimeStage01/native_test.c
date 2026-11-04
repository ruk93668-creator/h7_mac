//真实内核、Native适配与Initcall；仅替换USB业务和CPU上下文观察值
#include "stm32h7xx.h"
static uint32_t test_ipsr;
#define __get_IPSR() test_ipsr
#include "../../Middlewares/Third_Party/FreeRTOS/Source/tasks.c"
#include "../../CM7/Platform/platform_rtos.c"
#include "../../Common/Src/runtime_event.c"
#include "../../CM7/Application/app_events.c"
#include "../../CM7/Application/Tasks/camera_task.c"
#include "../../CM7/Application/Tasks/usb_task.c"
#include "../../CM7/Application/Tasks/control_task.c"
#include "../../CM7/Application/app_tasks.c"
#include "../../CM7/Application/app_main.c"
#include "../../CM7/Framework/framework_init.c"
#include "../../CM7/Platform/platform_hal_timebase.c"
#include <string.h>

uint32_t SystemCoreClock=480000000U,SystemD2Clock=240000000U,uwTickPrio;
static uint32_t usb_starts,usb_processes,usb_binds,hal_tick;
static UsbMode_t test_mode=USB_MODE_TEST;
static SysResult_t usb_start_result,bind_result,mode_result;
static UsbDeviceEventCallback_t saved_usb_callback;
static void *saved_usb_context;
static int32_t fail_level=-1;
static uint32_t init_seen[6];
volatile uint32_t g_test_case,g_fail_line,g_checks;
#define CHECK(x) do{g_checks++;if(!(x)){if(!g_fail_line)g_fail_line=__LINE__;return 1;}}while(0)

void *memset(void *d,int v,size_t n){volatile uint8_t *p=d;while(n--)*p++=(uint8_t)v;return d;}
void *memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;return d;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
void __aeabi_memset(void *d,size_t n,int v){volatile uint8_t *p=d;while(n--)*p++=(uint8_t)v;}
void __aeabi_memclr4(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr8(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *p=d;while(n--)*p++=0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){volatile uint8_t *p=d;const volatile uint8_t *q=s;while(n--)*p++=*q++;}
uint32_t HAL_RCC_GetHCLKFreq(void){return 240000000U;}
uint32_t HAL_RCC_GetPCLK1Freq(void){return 120000000U;}
void HAL_IncTick(void){hal_tick++;}
SysResult_t USB_Device_SetEventCallback(UsbDeviceEventCallback_t cb,void *context){
 usb_binds++;if(bind_result!=SYS_OK)return bind_result;saved_usb_callback=cb;saved_usb_context=context;return SYS_OK;
}
UsbMode_t USB_Mode_GetMode(void){return test_mode;}
SysResult_t USB_Mode_Start(void){usb_starts++;return usb_start_result;}
void USB_Mode_Process(void){usb_processes++;}
SysResult_t USB_Mode_GetLastResult(void){return mode_result;}
static SysResult_t MarkLevel(uint32_t level){init_seen[level]++;return fail_level==(int32_t)level?SYS_ERR_IO:SYS_OK;}
static SysResult_t Init0(void){return MarkLevel(0);}
static SysResult_t Init1(void){return MarkLevel(1);}
static SysResult_t Init2(void){return MarkLevel(2);}
static SysResult_t Init3(void){return MarkLevel(3);}
static SysResult_t Init4(void){return MarkLevel(4);}
static SysResult_t Init5(void){return MarkLevel(5);}
FRAMEWORK_INIT_REGISTER(Init0,INIT_LEVEL_PLATFORM);
FRAMEWORK_INIT_REGISTER(Init1,INIT_LEVEL_BUS);
FRAMEWORK_INIT_REGISTER(Init2,INIT_LEVEL_DEVICE);
FRAMEWORK_INIT_REGISTER(Init3,INIT_LEVEL_MEDIA);
FRAMEWORK_INIT_REGISTER(Init4,INIT_LEVEL_SERVICE);
FRAMEWORK_INIT_REGISTER(Init5,INIT_LEVEL_APPLICATION);
extern void vTaskSwitchContext(void);
extern BaseType_t xTaskIncrementTick(void);
extern void xPortSysTickHandler(void);

static int Create(void){CHECK(App_TasksCreate()==SYS_OK);CHECK(uxTaskGetNumberOfTasks()==3);CHECK(usb_starts==0);return 0;}
static int Start(bool prepare){
 CHECK(Create()==0);
 if(prepare){CHECK(Framework_InitRuntime()==SYS_OK);CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_OK);}
 //测试运行器只替换首次SVC，内核已运行但Platform函数会返回错误
 CHECK(Platform_RtosStart()==SYS_ERR_INVALID_STATE);
 CHECK(xTaskGetSchedulerState()==taskSCHEDULER_RUNNING);CHECK(uxTaskGetNumberOfTasks()==4);
 CHECK(xTaskGetCurrentTaskHandle()==s_usb_task);return 0;
}
//未模拟PendSV异常返回：API会提前执行恢复段，测试显式重建暂停在等待点的通知状态
//真实阻塞链表由内核API建立，只补回该模拟器无法保留的WAITING标志
static SysResult_t WaitBlocked(RuntimeEventTarget_t target,uint32_t ms,uint32_t *bits){
 TCB_t *waiting=pxCurrentTCB;
 SysResult_t result=App_EventsWait(target,ms,bits);
 if(result==SYS_ERR_TIMEOUT && ms!=0)waiting->ucNotifyState[0]=taskWAITING_NOTIFICATION;
 return result;
}
static int ToControl(void){
 uint32_t bits;
 CHECK(WaitBlocked(RUNTIME_TARGET_USB,APP_EVENTS_WAIT_FOREVER,&bits)==SYS_ERR_TIMEOUT);
 vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()==s_camera_task);
 CHECK(WaitBlocked(RUNTIME_TARGET_CAMERA,APP_EVENTS_WAIT_FOREVER,&bits)==SYS_ERR_TIMEOUT);
 vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()==s_control_task);return 0;
}
static int Boot(void){CHECK(Start(true)==0);CHECK(ToControl()==0);CHECK(App_TasksReleaseBoot()==SYS_OK);vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()==s_usb_task);return 0;}
int Native_TestRun(uint32_t id){
 uint32_t bits=0,i;StaticTask_t *tcb;StackType_t *stack;uint32_t size;
 g_test_case=id;
 if(id==0){CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_ERR_INVALID_STATE);CHECK(RuntimeEvent_Bind(NULL)==SYS_ERR_INVALID_ARG);return 0;}
 if(id>=1 && id<=3){CHECK(App_TasksCreate()==SYS_ERR_NO_RESOURCE);CHECK(g_app_tasks_debug.created_tasks==id-1);CHECK(uxTaskGetNumberOfTasks()==id-1);CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_ERR_INVALID_STATE);CHECK(App_TasksCreate()==SYS_ERR_INVALID_STATE);CHECK(!usb_starts && !s_boot_ready);return 0;}
 if(id==4){CHECK(Create()==0);CHECK(g_app_tasks_debug.prepared==0 && !s_boot_ready);return 0;}
 if(id==5){CHECK(Create()==0);CHECK(App_TasksCreate()==SYS_ERR_INVALID_STATE);CHECK(uxTaskGetNumberOfTasks()==3);return 0;}
 if(id==6){CHECK(Create()==0);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_OK);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_OK);CHECK(s_pending[RUNTIME_TARGET_USB]==RUNTIME_EVENT_WAKE);CHECK(!s_boot_ready && !usb_starts);return 0;}
 if(id==7){CHECK(Create()==0);CHECK(App_TasksRecordInitialization(SYS_ERR_IO)==SYS_ERR_IO);CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_ERR_INVALID_STATE);CHECK(App_TasksReleaseBoot()==SYS_ERR_INVALID_STATE);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_ERR_INVALID_STATE);CHECK(!s_boot_ready && !usb_starts);return 0;}
 if(id==8){CHECK(App_EventsInit()==SYS_OK);CHECK(App_EventsRecordInitialization(SYS_OK)==SYS_ERR_INVALID_STATE);CHECK(!s_prepared);return 0;}
 if(id==9){CHECK(Platform_RtosStart()==SYS_ERR_INVALID_STATE);CHECK(uxTaskGetNumberOfTasks()==0);return 0;}
 if(id==10){test_ipsr=16+OTG_FS_IRQn;CHECK(App_TasksCreate()==SYS_ERR_INVALID_STATE);CHECK(!s_create_attempted);return 0;}
 if(id==11){__disable_irq();CHECK(App_TasksCreate()==SYS_ERR_INVALID_STATE);CHECK(__get_PRIMASK()==1);__enable_irq();return 0;}
 if(id==12){CHECK(Create()==0);CHECK(RuntimeEvent_Post((RuntimeEventTarget_t)99,RUNTIME_EVENT_WAKE)==SYS_ERR_INVALID_ARG);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,0)==SYS_ERR_INVALID_ARG);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_BOOT_READY)==SYS_ERR_INVALID_ARG);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,1UL<<31)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==13){CHECK(Create()==0);test_ipsr=16+OTG_FS_IRQn;CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_ERR_INVALID_STATE);test_ipsr=0;CHECK(RuntimeEvent_PostFromIsr(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_ERR_INVALID_STATE);return 0;}
 if(id==14){bind_result=SYS_ERR_IO;CHECK(App_TasksCreate()==SYS_ERR_IO);CHECK(usb_binds==1 && !uxTaskGetNumberOfTasks() && !s_prepared);return 0;}
 if(id==15){CHECK(Create()==0);CHECK(RuntimeEvent_Bind(&s_event_port)==SYS_ERR_ALREADY_EXISTS);CHECK(App_EventsAttach(RUNTIME_TARGET_USB,s_usb_task)==SYS_ERR_ALREADY_EXISTS);return 0;}
 if(id==16){vApplicationGetIdleTaskMemory(&tcb,&stack,&size);CHECK(tcb==&s_idle_tcb && stack==s_idle_stack && size==256);CHECK(((uint32_t)stack&7)==0);return 0;}
 if(id==17){CHECK(Start(true)==0);CHECK(!s_boot_ready && !usb_starts);CHECK(Platform_RtosStart()==SYS_ERR_INVALID_STATE);return 0;}
 if(id==18){CHECK(Start(false)==0);CHECK(ToControl()==0);CHECK(App_TasksReleaseBoot()==SYS_ERR_INVALID_STATE);CHECK(!s_boot_ready && !usb_starts);return 0;}
 if(id==19){CHECK(Start(true)==0);CHECK(App_TasksReleaseBoot()==SYS_ERR_INVALID_STATE);CHECK(!s_boot_ready);return 0;}
 if(id==20){CHECK(Boot()==0);CHECK(g_app_tasks_debug.boot_released==1 && s_boot_ready);CHECK(!usb_starts);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);CHECK(bits&RUNTIME_EVENT_BOOT_READY);UsbTask_Process(bits);CHECK(usb_starts==1);return 0;}
 if(id==21){CHECK(Create()==0);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_OK);CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_OK);CHECK(Platform_RtosStart()==SYS_ERR_INVALID_STATE);CHECK(ToControl()==0);CHECK(App_TasksReleaseBoot()==SYS_OK);vTaskSwitchContext();CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);CHECK(bits==(RUNTIME_EVENT_BOOT_READY|RUNTIME_EVENT_WAKE));return 0;}
 if(id==22){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_CAMERA,0,&bits)==SYS_ERR_INVALID_STATE);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,NULL)==SYS_ERR_INVALID_ARG);return 0;}
 if(id==23){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);CHECK(WaitBlocked(RUNTIME_TARGET_USB,APP_EVENTS_WAIT_FOREVER,&bits)==SYS_ERR_TIMEOUT);vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()==s_camera_task);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_OK);vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()==s_usb_task);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK && bits==RUNTIME_EVENT_WAKE);return 0;}
 if(id==24){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);CHECK(WaitBlocked(RUNTIME_TARGET_USB,5,&bits)==SYS_ERR_TIMEOUT);vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()!=s_usb_task);for(i=0;i<4;i++)xTaskIncrementTick();vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()!=s_usb_task);xTaskIncrementTick();vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()==s_usb_task);return 0;}
 if(id==25){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);CHECK(RuntimeEvent_Post(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_OK);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK && bits==RUNTIME_EVENT_WAKE);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_ERR_TIMEOUT && !bits);return 0;}
 if(id==26){CHECK(Boot()==0);CHECK(RuntimeEvent_PostFromIsr(RUNTIME_TARGET_USB,RUNTIME_EVENT_WAKE)==SYS_ERR_INVALID_STATE);return 0;}
 if(id==27 || id==28){CHECK(Create()==0);NVIC_SetPriority(OTG_FS_IRQn,id==27?4:6);test_ipsr=16+OTG_FS_IRQn;CHECK(RuntimeEvent_PostFromIsr(RUNTIME_TARGET_USB,RUNTIME_EVENT_USB_PROGRESS)==(id==27?SYS_ERR_INVALID_STATE:SYS_OK));CHECK(s_pending[RUNTIME_TARGET_USB]==(id==27?0:RUNTIME_EVENT_USB_PROGRESS));CHECK(__get_PRIMASK()==0);return 0;}
 if(id==29){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);NVIC_SetPriority(OTG_FS_IRQn,6);test_ipsr=16+OTG_FS_IRQn;CHECK(RuntimeEvent_PostFromIsr(RUNTIME_TARGET_USB,RUNTIME_EVENT_USB_PROGRESS)==SYS_OK);test_ipsr=0;CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK && bits==RUNTIME_EVENT_USB_PROGRESS);return 0;}
 if(id==30){UsbTask_Process(RUNTIME_EVENT_USB_PROGRESS);CHECK(!usb_starts && !usb_processes && !g_usb_task_debug.boot_seen);return 0;}
 if(id==31){CHECK(Boot()==0);UsbTask_Process(RUNTIME_EVENT_BOOT_READY);UsbTask_Process(RUNTIME_EVENT_BOOT_READY);CHECK(usb_starts==1 && usb_processes==2);return 0;}
 if(id==32){CHECK(Boot()==0);usb_start_result=SYS_ERR_IO;UsbTask_Process(RUNTIME_EVENT_BOOT_READY);UsbTask_Process(0);CHECK(usb_starts==1 && !usb_processes && !s_usb_started);CHECK(g_usb_task_debug.result==SYS_ERR_IO);vTaskSuspend(s_usb_task);vTaskSuspend(s_camera_task);vTaskSwitchContext();CHECK(App_EventsWait(RUNTIME_TARGET_CONTROL,0,&bits)==SYS_OK && (bits&RUNTIME_EVENT_FAULT));return 0;}
 if(id==33){CHECK(Boot()==0);test_mode=USB_MODE_UVC;UsbTask_Process(RUNTIME_EVENT_BOOT_READY);CHECK(!usb_starts && !usb_processes && g_usb_task_debug.result==SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==34){CHECK(Boot()==0);UsbTask_Process(RUNTIME_EVENT_BOOT_READY);UsbTask_Process(0);CHECK(usb_starts==1 && usb_processes==2);return 0;}
 if(id==35){CHECK(Create()==0);CHECK(Framework_InitRuntime()==SYS_OK);CHECK(!usb_starts && !s_boot_ready);for(i=0;i<6;i++)CHECK(init_seen[i]==1);return 0;}
 if(id==36){test_mode=USB_MODE_UVC;CHECK(App_Init()==SYS_ERR_NOT_SUPPORTED);CHECK(!usb_starts);return 0;}
 if(id==37){CHECK(Boot()==0);__disable_irq();CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_ERR_INVALID_STATE);__enable_irq();return 0;}
 if(id==38){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK && bits&RUNTIME_EVENT_BOOT_READY);vTaskSuspend(s_usb_task);vTaskSwitchContext();CHECK(App_EventsWait(RUNTIME_TARGET_CAMERA,0,&bits)==SYS_OK && bits&RUNTIME_EVENT_BOOT_READY);vTaskSuspend(s_camera_task);vTaskSwitchContext();CHECK(App_TasksReleaseBoot()==SYS_ERR_ALREADY_EXISTS);CHECK(App_EventsWait(RUNTIME_TARGET_CONTROL,0,&bits)==SYS_OK && bits&RUNTIME_EVENT_BOOT_READY);return 0;}
 if(id==39){CHECK(sizeof(s_camera_stack)==4096 && sizeof(s_usb_stack)==8192 && sizeof(s_control_stack)==3072);CHECK(Create()==0);CHECK(s_tasks[0]==s_camera_task && s_tasks[1]==s_usb_task && s_tasks[2]==s_control_task);return 0;}
 if(id==40){CHECK(HAL_InitTick(15)==HAL_OK);CHECK(TIM6->PSC==239 && TIM6->ARR==999);TIM6->SR=TIM_SR_UIF;TIM6_DAC_IRQHandler();CHECK(hal_tick==1 && g_rtos_debug.tick_count==0);CHECK(NVIC_GetPriority(TIM6_DAC_IRQn)==4);return 0;}
 if(id==41){CHECK(Start(true)==0);xPortSysTickHandler();CHECK(g_rtos_debug.tick_count==1 && xTaskGetTickCount()==1 && hal_tick==0);return 0;}
 if(id==42){CHECK(Start(true)==0);CHECK(CameraTask_Create()==SYS_ERR_INVALID_STATE);CHECK(UsbTask_Create()==SYS_ERR_INVALID_STATE);CHECK(ControlTask_Create()==SYS_ERR_INVALID_STATE);CHECK(uxTaskGetNumberOfTasks()==4);return 0;}
 if(id==43){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);saved_usb_callback(USB_DEVICE_EVENT_STATE,saved_usb_context);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK && bits==RUNTIME_EVENT_USB_PROGRESS);return 0;}
 if(id==44){CHECK(Boot()==0);CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK);NVIC_SetPriority(OTG_FS_IRQn,6);test_ipsr=16+OTG_FS_IRQn;saved_usb_callback(USB_DEVICE_EVENT_IO,saved_usb_context);test_ipsr=0;CHECK(App_EventsWait(RUNTIME_TARGET_USB,0,&bits)==SYS_OK && bits==RUNTIME_EVENT_USB_PROGRESS);return 0;}
 if(id==45){CHECK(Create()==0);CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_OK);CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_ERR_INVALID_STATE);return 0;}
 if(id>=46 && id<52){CHECK(Create()==0);fail_level=id-46;CHECK(Framework_InitRuntime()==SYS_ERR_IO);CHECK(App_TasksRecordInitialization(SYS_ERR_IO)==SYS_ERR_IO);for(i=0;i<6;i++)CHECK(init_seen[i]==(i<=(uint32_t)fail_level?1:0));CHECK(!usb_starts && !s_boot_ready && !g_app_tasks_debug.prepared);return 0;}
 if(id==53){CHECK(Boot()==0);vTaskSuspend(s_usb_task);vTaskSwitchContext();CHECK(xTaskGetCurrentTaskHandle()==s_camera_task);CameraTask_Run(NULL);return 1;}
 if(id==54){CHECK(Boot()==0);UsbTask_Run(NULL);return 1;}
 if(id==55){CHECK(Start(true)==0);CHECK(ToControl()==0);ControlTask_Run(NULL);return 1;}
 if(id==52){CHECK(Boot()==0);UsbTask_Process(RUNTIME_EVENT_BOOT_READY);mode_result=SYS_ERR_TIMEOUT;UsbTask_Process(0);CHECK(g_usb_task_debug.result==SYS_ERR_TIMEOUT && s_usb_fault_reported);CHECK(usb_starts==1);return 0;}
 return 1;
}
