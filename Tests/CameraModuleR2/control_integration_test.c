//执行真实Control、旧消息队列和验收适配器，不操作硬件和文件系统
#include "../CameraLifecycleC1/transport_test.c"
#include R2_CONTROL_SOURCE
#include "HardwareControl.c"

//保存时间和Storage公共请求调用次数
static uint32_t now, storage_calls[4];
//提供测试时间
uint32_t Platform_TimeGetMs(void){return now;}
//提供测试内存屏障边界
void Platform_SyncMemoryBarrier(void){}
//接收真实Control发布的状态
SysResult_t App_StatusPublishControl(const AppControlStatus_t *status){(void)status;return SYS_OK;}
//提供USB UVC冷启动配置
const VideoConfig_t *VideoConfig_GetDefault(void)
{
    static const VideoConfig_t value={.output=VIDEO_OUTPUT_USB_UVC};return &value;
}
//记录录像请求
SysResult_t App_StorageStartRecording(void){storage_calls[0]++;return SYS_OK;}
//记录抓拍请求
SysResult_t App_StorageRequestSnapshot(void){storage_calls[1]++;return SYS_OK;}
//记录存储停止请求
SysResult_t App_StorageStop(void){storage_calls[2]++;return SYS_OK;}
//记录存储恢复请求
SysResult_t App_StorageRecover(void){storage_calls[3]++;return SYS_OK;}

//任务创建和阻塞出口不在本组执行，意外进入时保留失败
//拒绝测试范围外的任务登记
SysResult_t App_EventsAttach(RuntimeEventTarget_t target,TaskHandle_t handle){(void)target;(void)handle;g_failure=9101U;return SYS_ERR_INVALID_STATE;}
//拒绝测试范围外的阻塞等待
SysResult_t App_EventsWait(RuntimeEventTarget_t target,uint32_t timeout,uint32_t *events){(void)target;(void)timeout;(void)events;g_failure=9102U;return SYS_ERR_INVALID_STATE;}
//拒绝测试范围外的BOOT释放
SysResult_t App_TasksReleaseBoot(void){g_failure=9103U;return SYS_ERR_INVALID_STATE;}
//拒绝测试范围外的栈扫描
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle){(void)handle;g_failure=9104U;return 0U;}
//拒绝测试范围外的任务创建
TaskHandle_t xTaskCreateStatic(TaskFunction_t fn,const char *name,const uint32_t depth,void *arg,UBaseType_t priority,StackType_t *stack,StaticTask_t *tcb)
{(void)fn;(void)name;(void)depth;(void)arg;(void)priority;(void)stack;(void)tcb;g_failure=9105U;return NULL;}

//通过真实相机或USB消息通道返回匹配ACK
static SysResult_t Ack(RuntimeEventTarget_t target,AppMessageType_t type)
{
    AppMessage_t message;AppChannel_t channel=target==RUNTIME_TARGET_CAMERA?APP_CHANNEL_CAMERA_COMMAND:APP_CHANNEL_USB_COMMAND;
    owner=target;
    if(!App_MessagesReceive(channel,&message) || message.type!=type)return SYS_ERR_PROTOCOL;
    message.type=APP_MESSAGE_ACK;message.source=target;message.result=SYS_OK;
    if(App_MessagesSend(APP_CHANNEL_CONTROL,&message)!=SYS_OK)return SYS_ERR_NO_RESOURCE;
    owner=RUNTIME_TARGET_CONTROL;ControlTask_Process(0U);return SYS_OK;
}

//验证验收投递与真实Control决策、消息权限及队列满行为
int R2_ControlIntegrationRun(uint32_t id)
{
    AppMessage_t message={.type=APP_MESSAGE_ACK,.source=RUNTIME_TARGET_CONTROL};uint32_t i;
    CHECK(App_MessagesInit()==SYS_OK);scheduler=taskSCHEDULER_RUNNING;owner=RUNTIME_TARGET_CONTROL;
    if(id==0U)
    {
        for(i=0U;i<4U;i++)CHECK(App_MessagesSend(APP_CHANNEL_CONTROL,&message)==SYS_OK);
        g_r2_hw.command=2U;g_r2_hw.submit_seq=1U;
        CHECK(R2_HardwareControlProcess()==SYS_ERR_NO_RESOURCE);
        CHECK(g_r2_hw.processed_seq==1U && uxQueueMessagesWaiting(s_message_handles[APP_CHANNEL_CONTROL])==4U);
        CHECK(R2_HardwareControlProcess()==SYS_OK && uxQueueMessagesWaiting(s_message_handles[APP_CHANNEL_CONTROL])==4U);return 0;
    }
    ControlTask_Process(RUNTIME_EVENT_BOOT_READY);
    CHECK(Ack(RUNTIME_TARGET_CAMERA,APP_MESSAGE_START)==SYS_OK);
    CHECK(Ack(RUNTIME_TARGET_USB,APP_MESSAGE_START)==SYS_OK && s_control_state==CONTROL_RUNNING);
    if(id==1U)
    {
        g_r2_hw.command=2U;g_r2_hw.submit_seq=1U;
        CHECK(R2_HardwareControlProcess()==SYS_OK && s_control_state==CONTROL_RUNNING);
        CHECK(uxQueueMessagesWaiting(s_message_handles[APP_CHANNEL_CONTROL])==1U);
        ControlTask_Process(0U);CHECK(s_control_state==CONTROL_STOP_USB);
        CHECK(Ack(RUNTIME_TARGET_USB,APP_MESSAGE_STOP)==SYS_OK && s_control_state==CONTROL_STOP_CAMERA);
        CHECK(Ack(RUNTIME_TARGET_CAMERA,APP_MESSAGE_STOP)==SYS_OK && s_control_state==CONTROL_IDLE);
        g_r2_hw.command=1U;g_r2_hw.submit_seq=2U;
        CHECK(R2_HardwareControlProcess()==SYS_OK && s_control_state==CONTROL_IDLE);
        ControlTask_Process(0U);CHECK(s_control_state==CONTROL_START_CAMERA);
        CHECK(Ack(RUNTIME_TARGET_CAMERA,APP_MESSAGE_START)==SYS_OK);
        CHECK(Ack(RUNTIME_TARGET_USB,APP_MESSAGE_START)==SYS_OK && s_control_state==CONTROL_RUNNING);return 0;
    }
    if(id==2U)
    {
        for(i=0U;i<4U;i++)
        {
            g_r2_hw.command=i+3U;g_r2_hw.submit_seq=i+1U;
            CHECK(R2_HardwareControlProcess()==SYS_OK && storage_calls[i]==1U && s_control_state==CONTROL_RUNNING);
        }
        return 0;
    }
    owner=RUNTIME_TARGET_USB;g_r2_hw.command=2U;g_r2_hw.submit_seq=1U;
    CHECK(R2_HardwareControlProcess()==SYS_ERR_INVALID_STATE && !g_r2_hw.processed_seq);
    owner=RUNTIME_TARGET_CONTROL;basepri=0x50U;
    CHECK(R2_HardwareControlProcess()==SYS_ERR_INVALID_STATE && !g_r2_hw.processed_seq);
    basepri=0U;CHECK(R2_HardwareControlProcess()==SYS_OK);return 0;
}
