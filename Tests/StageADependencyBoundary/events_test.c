//复用真实Media和同步边界，再串联真实事件适配及RuntimeEvent后端
#include "boundary_test.c"
#include "../../CM7/Application/app_events.c"
#include "../../Common/Src/runtime_event.c"

//仅替代RTOS投递原语，观察BOOT、任务通知与ISR通知的边界
static uint32_t notices[5], task_posts, irq_posts;
static int scheduler;
static TaskHandle_t current;
int xTaskGetSchedulerState(void) { return scheduler; }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current; }
BaseType_t xTaskNotify(TaskHandle_t task,uint32_t bits,eNotifyAction action)
{ (void)action;notices[(uint32_t)task-1U]|=bits;task_posts++;return pdPASS; }
BaseType_t xTaskNotifyFromISR(TaskHandle_t task,uint32_t bits,eNotifyAction action,BaseType_t *wake)
{ (void)action;notices[(uint32_t)task-1U]|=bits;irq_posts++;*wake=pdFALSE;return pdPASS; }
BaseType_t xTaskNotifyWait(uint32_t a,uint32_t b,uint32_t *value,TickType_t ticks)
{ (void)a;(void)b;(void)ticks;*value=0;return pdTRUE; }
SysResult_t USB_Device_SetEventCallback(UsbDeviceEventCallback_t cb,void *context)
{ (void)cb;(void)context;return SYS_OK; }
int Events_Test(uint32_t id)
{
    uint32_t i;
    if (id==0U) { bind_result=SYS_ERR_IO;CHECK(App_EventsInit()==SYS_ERR_IO);CHECK(!s_initialized && s_capture_callback!=NULL);return 0; }
    CHECK(App_EventsInit()==SYS_OK);
    CHECK(bind_calls==1U && bound!=NULL);
    for(i=0;i<RUNTIME_TARGET_COUNT;i++)CHECK(App_EventsAttach((RuntimeEventTarget_t)i,(TaskHandle_t)(i+1U))==SYS_OK);
    if(id==1U) { CHECK(App_EventsRecordInitialization(SYS_ERR_IO)==SYS_ERR_IO);scheduler=taskSCHEDULER_RUNNING;current=(void*)3U;CHECK(App_EventsReleaseBoot()==SYS_ERR_INVALID_STATE);CHECK(task_posts==0U);return 0; }
    if(id==2U || id==3U)ipsr=37U;
    bound(BSP_DCMI_NOTIFY_PROGRESS,bound_context);
    bound(BSP_DCMI_NOTIFY_ERROR,bound_context);
    CHECK(task_posts==0U && irq_posts==0U);
    ipsr=0U;
    CHECK(App_EventsRecordInitialization(SYS_OK)==SYS_OK);
    scheduler=taskSCHEDULER_RUNNING;current=(void*)3U;
    CHECK(App_EventsReleaseBoot()==SYS_OK);
    CHECK(task_posts==RUNTIME_TARGET_COUNT && irq_posts==0U);
    CHECK((notices[RUNTIME_TARGET_CAMERA]&(RUNTIME_EVENT_BOOT_READY|RUNTIME_EVENT_CAMERA_PROGRESS))==(RUNTIME_EVENT_BOOT_READY|RUNTIME_EVENT_CAMERA_PROGRESS));
    CHECK(App_EventsReleaseBoot()==SYS_ERR_ALREADY_EXISTS);
    notices[RUNTIME_TARGET_CAMERA]=0U;
    if(id==3U || id==4U)ipsr=37U;
    bound(BSP_DCMI_NOTIFY_PROGRESS|BSP_DCMI_NOTIFY_ERROR,bound_context);
    CHECK(notices[RUNTIME_TARGET_CAMERA]==RUNTIME_EVENT_CAMERA_PROGRESS);
    CHECK(irq_posts==((id==3U||id==4U)?1U:0U));
    CHECK(task_posts==RUNTIME_TARGET_COUNT+((id==3U||id==4U)?0U:1U));
    return 0;
}
