//执行真实CameraTask、App_Camera和Media；硬件与帧引用边界使用原B1桩
#include R2_MEDIA_FIXTURE
#include "app_camera_qualification_internal.h"

//统计真实Media下发BSP停止的次数，独立于Stop状态检查
static uint32_t hardware_stops;
static uint32_t device_before_media;
static uint32_t stop_state_before;

//记录实际硬件停止调用，不模拟Media状态转移
SysResult_t BSP_DCMI_Stop(void)
{
    hardware_stops++;
    stop_state_before=s_capture.state;
    if (sensor.base.state!=DEVICE_STATE_RUNNING) device_before_media++;
    return R2_FixtureBspStop();
}

//读取原依赖轨迹中的调用次数
static uint32_t Count(uint32_t code)
{
    uint32_t i,n=0U;
    for(i=0U;i<g_trace_count;i++) if(g_trace[i][0]==code)n++;
    return n;
}

//显式保留任务的Media、Bridge、轮末顺序
static void Advance(void)
{
    App_CameraProcess();
    App_UvcBridgeCameraProcess(App_CameraCanOfferFrame());
    App_CameraFinishRound();
}

//提供四字段完整身份
static AppLifecycleIdentity_t Identity(void)
{
    AppLifecycleIdentity_t identity={1U,8U,APP_CONSUMER_USB,3U};
    return identity;
}

//专项验证停止事实、同轮去重和请求结果冻结
int R2_StopRun(uint32_t id)
{
    CameraCaptureStopFacts_t facts,before;
    AppQualificationStopResult_t result;
    AppLifecycleIdentity_t identity=Identity(),wrong;
    uint32_t n;
    sensor.base.state=DEVICE_STATE_READY;
    config.camera.format=CAMERA_PIXEL_FORMAT_JPEG;
    App_CameraSetDiagnosticObserver(CameraTask_Observe);

    //未初始化的查询不声称硬件已经停止
    if(id==20U)
    {
        CHECK(CameraCapture_GetStopFacts(NULL)==SYS_ERR_INVALID_ARG);
        CHECK(CameraCapture_GetStopFacts(&facts)==SYS_OK && facts.flags==0U && facts.attempt_sequence==0U);
        CHECK(App_CameraStop()==SYS_ERR_INVALID_STATE);return 0;
    }
    if(id==8U || id==21U)
    {
        config.camera.format=CAMERA_PIXEL_FORMAT_YUV422;
        start_result=SYS_ERR_IO;
        if(id==8U)stop_error_rounds=1U;
        Push(APP_MESSAGE_START,1U);Round(RUNTIME_EVENT_BOOT_READY);
        CHECK(hardware_stops==1U);
        CHECK(Count(15U)==1U);//回退失败也必须继续Bridge归还
        CHECK(s_camera.last_result==SYS_ERR_IO);
        CHECK(CameraCapture_GetStopFacts(&facts)==SYS_OK && facts.attempt_sequence==1U);
        if(id==8U)CHECK(facts.hardware_result==SYS_ERR_IO);
        return 0;
    }
    Push(APP_MESSAGE_START,1U);Round(RUNTIME_EVENT_BOOT_READY);
    CHECK(s_camera.running && hardware_stops==0U);

    if(id==0U || id==3U)
    {
        App_CameraBeginRound();
        CHECK(App_CameraStop()==SYS_ERR_BUSY && hardware_stops==0U);
        if(id==3U)CHECK(App_CameraStop()==SYS_ERR_BUSY && hardware_stops==0U);
        Advance();CHECK(hardware_stops==1U && CameraCapture_IsStopped());
        CHECK(CameraCapture_GetStopFacts(&facts)==SYS_OK && (facts.flags&CAMERA_STOP_NORMAL_ATTEMPT)!=0U);
        App_CameraFinishRound();CHECK(hardware_stops==1U);return 0;
    }
    if(id==1U)
    {
        jpeg_loan=true;Push(APP_MESSAGE_STOP,2U);Round(0U);
        CHECK(hardware_stops==0U && s_camera.stopping);
        jpeg_loan=false;Round(0U);CHECK(hardware_stops==1U && !s_camera.stopping);return 0;
    }
    if(id==2U)
    {
        stop_error_rounds=1U;Push(APP_MESSAGE_STOP,2U);Round(0U);
        CHECK(hardware_stops==1U && s_camera.last_result==SYS_ERR_IO && s_camera.stopping);
        Round(0U);CHECK(hardware_stops==2U && !s_camera.stopping);
        CHECK(s_camera.last_result==SYS_ERR_IO && s_camera.fault==SYS_ERR_IO);return 0;
    }
    if(id>=4U && id<=7U)
    {
        injected_error=1U;
        if(id==4U)Push(APP_MESSAGE_STOP,2U);
        if(id==6U)stop_error_rounds=1U;
        if(id==7U)jpeg_loan=true;
        Round(0U);CHECK(hardware_stops==1U && s_camera.fault==SYS_ERR_IO);
        CHECK(CameraCapture_GetStopFacts(&facts)==SYS_OK && (facts.flags&CAMERA_STOP_PROTECTIVE_ATTEMPT)!=0U);
        if(id==6U)
        {
            CHECK(s_capture.state==CAMERA_CAPTURE_ERROR && sensor.base.state==DEVICE_STATE_RUNNING);
            CHECK(CameraCapture_GetStopFacts(&facts)==SYS_OK && facts.hardware_result==SYS_ERR_IO);
            Round(0U);CHECK(hardware_stops==2U);
        }
        if(id==7U)
        {
            CHECK(CameraCapture_GetStopFacts(&facts)==SYS_OK && (facts.flags&CAMERA_STOP_DRAINING)!=0U);
            CHECK(!CameraCapture_IsStopped() && sensor.base.state==DEVICE_STATE_RUNNING);
            CHECK(CameraCapture_StreamReleaseFrame()==SYS_OK);Round(0U);CHECK(hardware_stops==1U);
        }
        CHECK(CameraCapture_IsStopped() && sensor.base.state==DEVICE_STATE_READY);return 0;
    }
    if(id==9U)
    {
        device_stop_result=SYS_ERR_IO;Push(APP_MESSAGE_STOP,2U);Round(0U);
        CHECK(hardware_stops==1U && CameraCapture_IsStopped());
        CHECK(s_camera.last_result==SYS_ERR_IO && sensor.base.state==DEVICE_STATE_ERROR);
        CHECK(Count(23U)==1U);return 0;
    }
    if(id==10U)
    {
        bridge_idle=false;Push(APP_MESSAGE_STOP,2U);Round(0U);
        CHECK(hardware_stops==1U && s_camera.stopping && s_camera.stop_command);
        bridge_idle=true;Round(0U);CHECK(hardware_stops==1U && !s_camera.stopping);return 0;
    }

    //以下只验证App内部资源适配，未将资格消息接入正式任务
    App_CameraBeginRound();
    CHECK(App_CameraRequestStop(NULL,&identity)==SYS_OK && hardware_stops==0U);
    CHECK(App_CameraReadStopResult(NULL,&identity,&result)==SYS_OK && !result.complete);
    if(id==11U)
    {
        CHECK(App_CameraRequestStop(NULL,&identity)==SYS_OK);
        CHECK(App_CameraConsumeStopResult(NULL,&identity)==SYS_ERR_PROTOCOL);
    }
    if(id==12U)stop_error_rounds=1U;
    if(id==13U)bridge_idle=false;
    if(id==14U)jpeg_loan=true;
    if(id==15U)
    {
        wrong=identity;wrong.request_id++;
        CHECK(App_CameraRequestStop(NULL,&wrong)==SYS_ERR_BUSY);
        CHECK(App_CameraReadStopResult(NULL,&wrong,&result)==SYS_ERR_PROTOCOL);
        CHECK(App_CameraConsumeStopResult(NULL,&wrong)==SYS_ERR_PROTOCOL);
    }
    Advance();
    if(id==12U)
    {
        CHECK(App_CameraReadStopResult(NULL,&identity,&result)==SYS_OK && result.complete && result.result==SYS_ERR_IO);
        Round(0U);CHECK(!s_camera.stopping && hardware_stops==2U);
        CHECK(App_CameraReadStopResult(NULL,&identity,&result)==SYS_OK && result.result==SYS_ERR_IO && !result.hardware_stopped);
    }
    else if(id==13U || id==14U)
    {
        CHECK(App_CameraReadStopResult(NULL,&identity,&result)==SYS_OK && !result.complete);
        bridge_idle=true;jpeg_loan=false;Round(0U);
    }
    if(id!=12U)CHECK(App_CameraReadStopResult(NULL,&identity,&result)==SYS_OK && result.complete && result.result==SYS_OK && result.hardware_stopped);
    if(id==16U || id==17U || id==18U || id==19U)
    {
        wrong=identity;
        if(id==16U)wrong.session_id++;
        if(id==17U)wrong.consumer_generation++;
        if(id==18U)wrong.consumer_id=APP_CONSUMER_STORAGE;
        if(id==19U)wrong.request_id++;
        CHECK(App_CameraReadStopResult(NULL,&wrong,&result)==SYS_ERR_PROTOCOL);
        CHECK(App_CameraConsumeStopResult(NULL,&wrong)==SYS_ERR_PROTOCOL);
        CHECK(s_camera.stop_result_valid);
    }
    CHECK(App_CameraConsumeStopResult(NULL,&identity)==SYS_OK);
    CHECK(App_CameraReadStopResult(NULL,&identity,&result)==SYS_ERR_NOT_FOUND);
    CHECK(App_CameraConsumeStopResult(NULL,&identity)==SYS_ERR_PROTOCOL);
    if(id==22U)
    {
        n=hardware_stops;Push(APP_MESSAGE_START,3U);Round(0U);
        CHECK(s_camera.running && hardware_stops==n && sensor.base.state==DEVICE_STATE_RUNNING);
    }
    if(id==24U)
    {
        //只比较本轮序号是否变化，UINT32_MAX到0不会变成同一轮未尝试
        Push(APP_MESSAGE_START,3U);Round(0U);
        s_stop_attempt_sequence=UINT32_MAX;
        stop_error_rounds=1U;injected_error=1U;Round(0U);
        CHECK(s_stop_attempt_sequence==0U && hardware_stops==2U);
        CHECK(s_camera.stopping && s_camera.fault==SYS_ERR_IO);
    }
    if(id==23U)
    {
        CHECK(CameraCapture_GetStopFacts(&before)==SYS_OK);
        App_CameraBeginRound();Advance();
        CHECK(CameraCapture_GetStopFacts(&facts)==SYS_OK);
        CHECK(memcmp(&before,&facts,sizeof(facts))==0);
    }
    CHECK(device_before_media==0U);
    return 0;
}
