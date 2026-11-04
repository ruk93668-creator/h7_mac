#include "../DcmiReplaceTarget/dcmi_test.c"
static uint32_t notification_count,notification_flags;
static void *notification_context;
static void NotifyError(uint32_t e,void *p){notification_count++;notification_flags=e;notification_context=p;}
int Stage02_TestRun(uint32_t id)
{
    if(id==0){EQ(BSP_DCMI_SetEventCallback(NULL,NULL),SYS_ERR_INVALID_ARG);return 0;}
    if(id==1){mock_ipsr=16;EQ(BSP_DCMI_SetEventCallback(NotifyError,NULL),SYS_ERR_INVALID_STATE);return 0;}
    if(id==2){s_dcmi_ready=true;EQ(BSP_DCMI_SetEventCallback(NotifyError,NULL),SYS_ERR_INVALID_STATE);return 0;}
    if(id==3){s_capture_active=true;EQ(BSP_DCMI_SetEventCallback(NotifyError,NULL),SYS_ERR_INVALID_STATE);return 0;}
    EQ(BSP_DCMI_SetEventCallback(NotifyError,(void *)0x1234),SYS_OK);
    if(id==4){EQ(BSP_DCMI_SetEventCallback(NotifyError,NULL),SYS_ERR_INVALID_STATE);return 0;}
    s_stream.mode=BSP_DCMI_MODE_CONTINUOUS;s_event_flags=BSP_DCMI_EVENT_FRAME;
    Dcmi_RecordError(SYS_ERR_IO);
    EQ(notification_count,1);EQ(notification_flags,BSP_DCMI_NOTIFY_ERROR);EQ(notification_context,(void *)0x1234);
    EQ(s_event_flags,BSP_DCMI_EVENT_ERROR);EQ(s_stop_required,true);EQ(s_stream.state,BSP_DCMI_STREAM_ERROR);
    if(id==5)return 0;
    Dcmi_RecordError(SYS_ERR_TIMEOUT);EQ(s_event_result,SYS_ERR_IO);EQ(notification_count,2);return 0;
}
