//只链接未注入故障的生产实现，核对真实ABI和静态调用图
#include "app_camera_lifecycle_replay.h"
#include <string.h>

//这些实例只属于独立ABI探针，不在正式固件中分配
static AppLifecycleTxContext_t s_probe_tx;
static AppLifecycleRxContext_t s_probe_rx;
static AppLifecycleMessage_t s_probe_message;
static AppLifecycleReplayStatus_t s_probe_status;
volatile uint32_t g_failure;
volatile uint32_t g_layout[16];

//保留各生产入口，case0只输出布局，其他分支仅用于链接调用图
uint32_t C2_LayoutRun(uint32_t id)
{
    switch(id)
    {
        case 0:
            g_layout[0]=sizeof(AppLifecycleTxContext_t);
            g_layout[1]=sizeof(AppLifecycleRxContext_t);
            g_layout[2]=sizeof(AppLifecycleMessage_t);
            g_layout[3]=sizeof(AppLifecyclePending_t);
            g_layout[4]=sizeof(AppLifecycleReplay_t);
            g_layout[5]=offsetof(AppLifecycleTxContext_t,pending);
            g_layout[6]=offsetof(AppLifecycleRxContext_t,replay);
            g_layout[7]=offsetof(AppLifecycleRxContext_t,reply_pending);
            g_layout[8]=offsetof(AppLifecycleRxContext_t,candidate);
            g_layout[9]=(uint32_t)&s_probe_tx;
            g_layout[10]=(uint32_t)&s_probe_rx;
            return g_failure;
        case 1:return App_LifecycleTxInit(&s_probe_tx,2U,1U,1U);
        case 2:return App_LifecycleTxReserveIds(&s_probe_tx,1U,(uint32_t *)&g_layout[15]);
        case 3:return App_LifecycleTxStage(&s_probe_tx,&s_probe_message);
        case 4:return App_LifecycleTxFlush(&s_probe_tx,NULL,NULL);
        case 5:return App_LifecycleTxAcceptReply(&s_probe_tx,&s_probe_message);
        case 6:return App_LifecycleTxGetCompleted(&s_probe_tx,&s_probe_message);
        case 7:return App_LifecycleTxConfirmConsumed(&s_probe_tx,&s_probe_message);
        case 8:return App_LifecycleTxCancelReady(&s_probe_tx,&s_probe_message);
        case 9:return App_LifecycleRxInit(&s_probe_rx,2U,1U,1U);
        case 10:return App_LifecycleRxReceiveRequest(&s_probe_rx,&s_probe_message,(uint32_t *)&g_layout[15]);
        case 11:return App_LifecycleRxPromote(&s_probe_rx,&s_probe_message);
        case 12:return App_LifecycleRxComplete(&s_probe_rx,&s_probe_message);
        case 13:return App_LifecycleRxFlushReply(&s_probe_rx,NULL,NULL);
        case 14:return App_LifecycleRxGetStatus(&s_probe_rx,&s_probe_status);
        default:return 1U;
    }
}
