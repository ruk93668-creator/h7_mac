//复用原真实FreeRTOS队列测试依赖，生产实现通过C1_MESSAGES_SOURCE编译
#include "../CameraLifecycleC1/transport_test.c"

//验证初始化放行不改变BASEPRI，也不放宽运行期收发保护
int Basepri_InitRun(uint32_t id)
{
    AppLifecycleMessage_t m=Life(1U,APP_LIFECYCLE_ACQUIRE,APP_LIFECYCLE_REQUEST),r,before;
    uint32_t count;
    if(id==0U || id==1U || id==2U)
    {
        uint32_t expected=id==0U?0U:id==1U?0x50U:0x80U;
        basepri=expected;
        CHECK(App_LifecycleMessagesInit()==SYS_OK);
        CHECK(basepri==expected && creates==3U && s_lifecycle_ready && s_lifecycle_attempted);
        CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE && creates==3U);
        CHECK(basepri==expected);return 0;
    }
    if(id>=3U && id<=6U)
    {
        basepri=0x50U;
        if(id==3U)primask=1U;
        if(id==4U)exception=16U;
        if(id==5U)scheduler=taskSCHEDULER_RUNNING;
        if(id==6U)scheduler=taskSCHEDULER_SUSPENDED;
        CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE);
        CHECK(!s_lifecycle_attempted && !s_lifecycle_ready && creates==0U && basepri==0x50U);
        primask=exception=0U;scheduler=taskSCHEDULER_NOT_STARTED;
        CHECK(App_LifecycleMessagesInit()==SYS_OK && basepri==0x50U);return 0;
    }
    if(id==7U)
    {
        fail_create=2U;basepri=0x50U;
        CHECK(App_LifecycleMessagesInit()==SYS_ERR_NO_RESOURCE && s_lifecycle_attempted && !s_lifecycle_ready);
        count=creates;CHECK(App_LifecycleMessagesInit()==SYS_ERR_INVALID_STATE && creates==count);
        CHECK(basepri==0x50U);return 0;
    }
    CHECK(App_LifecycleMessagesInit()==SYS_OK);
    scheduler=taskSCHEDULER_RUNNING;owner=RUNTIME_TARGET_USB;
    if(id==8U || id==9U)
    {
        basepri=id==8U?0x50U:1U;count=posts;
        CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==SYS_ERR_INVALID_STATE);
        CHECK(uxQueueMessagesWaiting(s_lifecycle_handles[0])==0U && posts==count);
        CHECK(basepri==(id==8U?0x50U:1U));
        basepri=0U;CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==SYS_OK);return 0;
    }
    CHECK(App_LifecycleMessagesSend(APP_LIFECYCLE_CHANNEL_CAMERA,&m)==SYS_OK);
    owner=RUNTIME_TARGET_CAMERA;memset(&r,0x5A,sizeof(r));before=r;
    if(id==10U || id==11U)
    {
        basepri=id==10U?0x50U:1U;
        CHECK(!App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_CAMERA,&r));
        CHECK(memcmp(&r,&before,sizeof(r))==0 && uxQueueMessagesWaiting(s_lifecycle_handles[0])==1U);
        CHECK(basepri==(id==10U?0x50U:1U));
        basepri=0U;
    }
    CHECK(App_LifecycleMessagesReceive(APP_LIFECYCLE_CHANNEL_CAMERA,&r));
    CHECK(memcmp(&m,&r,sizeof(m))==0 && uxQueueMessagesWaiting(s_lifecycle_handles[0])==0U);
    CHECK(sizeof(AppLifecycleMessage_t)==56U && sizeof(AppMessage_t)==52U);
    return 0;
}
