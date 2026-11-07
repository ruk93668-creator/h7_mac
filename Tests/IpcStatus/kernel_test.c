#define IPC_STATUS_REAL_APP 1
#include "../IpcRuntime/task_test.c"
#include "../../CM7/Application/app_status.c"

//使用真实任务TCB和实际Application状态接口，直接选择当前任务供模拟器执行
int IpcTaskStatus_TestRun(uint32_t id)
{
    AppCameraStatus_t camera={1U,15000U,100U,2U,640U,480U,CAMERA_PIXEL_FORMAT_JPEG,true,SYS_OK};
    AppUsbStatus_t usb={1U,16000U,90U,SYS_OK};
    AppControlStatus_t control={3U,SYS_OK,SYS_ERR_IO};
    IpcApplicationStatus_t a,b;
    uint32_t before;
    if(id<20U)return IpcTask_TestRun(id);
    g_case=id;
    CHECK(App_TasksCreate()==SYS_OK);
    CHECK(App_TasksRecordInitialization(SYS_OK)==SYS_OK);
    Running();CHECK(App_TasksReleaseBoot()==SYS_OK);
    pxCurrentTCB=s_tasks[RUNTIME_TARGET_IPC];
    CHECK(App_StatusGetSnapshot(&a)==SYS_OK && a.valid_mask==0U);
    if(id==20U)return 0;
    if(id==21U){CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==22U){test_ipsr=16U;CHECK(App_StatusGetSnapshot(&a)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==23U){__disable_irq();CHECK(App_StatusGetSnapshot(&a)==SYS_ERR_INVALID_STATE);__enable_irq();return 0;}
    if(id==24U){CHECK(App_StatusGetSnapshot((IpcApplicationStatus_t *)0x38000200UL)==SYS_ERR_INVALID_ARG);return 0;}
    pxCurrentTCB=s_tasks[RUNTIME_TARGET_CAMERA];s_tick=100U;
    CHECK(App_StatusPublishCamera(&camera)==SYS_OK);
    pxCurrentTCB=s_tasks[RUNTIME_TARGET_USB];s_tick=110U;
    CHECK(App_StatusPublishUsb(&usb)==SYS_OK);
    pxCurrentTCB=s_tasks[RUNTIME_TARGET_CONTROL];s_tick=120U;
    CHECK(App_StatusPublishControl(&control)==SYS_OK);
    pxCurrentTCB=s_tasks[RUNTIME_TARGET_IPC];s_tick=130U;
    CHECK(App_StatusGetSnapshot(&a)==SYS_OK && a.valid_mask==IPC_APP_ALL);
    CHECK(a.width==640U && a.height==480U && a.format==IPC_IMAGE_JPEG);
    CHECK(a.camera_age_ms==30U && a.usb_age_ms==20U && a.control_age_ms==10U);
    CHECK(a.camera_fps_milli==15000U && a.usb_fps_milli==16000U);
    CHECK(a.captured_frames==100U && a.usb_completed_frames==90U && a.last_fault==SYS_ERR_IO);
    if(id==25U)return 0;
    if(id==26U){pxCurrentTCB=s_tasks[RUNTIME_TARGET_USB];CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==27U){pxCurrentTCB=s_tasks[RUNTIME_TARGET_CAMERA];CHECK(App_StatusGetSnapshot(&b)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==28U){s_tick=600U;CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.camera_age_ms==UINT32_MAX && b.usb_age_ms==490U);return 0;}
    if(id==29U)
    {
        pxCurrentTCB=s_tasks[RUNTIME_TARGET_CAMERA];camera.captured_frames=UINT32_MAX;CHECK(App_StatusPublishCamera(&camera)==SYS_OK);
        camera.captured_frames=0U;CHECK(App_StatusPublishCamera(&camera)==SYS_OK);
        pxCurrentTCB=s_tasks[RUNTIME_TARGET_IPC];CHECK(App_StatusGetSnapshot(&b)==SYS_OK && b.captured_frames==0U);return 0;
    }
    if(id==30U){pxCurrentTCB=s_tasks[RUNTIME_TARGET_CONTROL];control.state=7U;CHECK(App_StatusPublishControl(&control)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==31U){pxCurrentTCB=s_tasks[RUNTIME_TARGET_USB];usb.result=1;CHECK(App_StatusPublishUsb(&usb)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==32U){pxCurrentTCB=s_tasks[RUNTIME_TARGET_CAMERA];camera.format=(CameraPixelFormat_t)99;CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==33U){CHECK(App_StatusGetSnapshot((IpcApplicationStatus_t *)0xFFFFFFF0UL)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==34U){before=a.snapshot_id;CHECK(App_StatusGetSnapshot(&b)==SYS_OK && b.snapshot_id==before+1U);return 0;}
    if(id==35U)
    {
        taskENTER_CRITICAL();s_snapshot_id=UINT32_MAX;taskEXIT_CRITICAL();
        CHECK(App_StatusGetSnapshot(&b)==SYS_OK && b.snapshot_id==0U);return 0;
    }
    if(id==36U){CHECK(App_StatusGetSnapshotAt(&b,&before)==SYS_OK && before==130U);return 0;}
    if(id==37U){CHECK(App_StatusGetSnapshotAt(&b,(uint32_t *)0x38000200UL)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==38U){CHECK(App_StatusGetSnapshotAt(&b,&b.snapshot_id)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==39U){CHECK(App_StatusGetSnapshotAt(&b,NULL)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==40U){s_tick=0x20U;CHECK(App_StatusGetSnapshotAt(&b,&before)==SYS_OK && before==0x20U);return 0;}
    return 1;
}
