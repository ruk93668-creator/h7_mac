#define IPC_TestAfterAccess Ipc_PreviousAccessHook
#define Ipc_TestRun Ipc_PreviousTestRun
#include "../IpcV2/protocol_test.c"
#undef Ipc_TestRun
#undef IPC_TestAfterAccess
#include "app_status.h"
#include "ipc_runtime.h"

//任务和时间仅由本用例控制，协议与Application代码使用正式实现
volatile uint32_t g_app_critical;
static RuntimeEventTarget_t s_app_owner=RUNTIME_TARGET_IPC;
#if defined(CORE_CM7)
static uint32_t s_now;
#endif
static bool s_owner=true;
static bool s_interleave_busy;
static uint32_t s_interleave_word;
static uint32_t s_interleave_mode;
static uint32_t s_interleave_fail;
static IpcApplicationStatus_t s_next;
static uint32_t s_defer_collect;
static IpcApplicationStatus_t s_deferred_snapshot;

bool App_EventsIsOwner(RuntimeEventTarget_t target)
{
    return s_thread && g_app_critical==0U && target==s_app_owner;
}
void AppTest_EnterCritical(void){g_app_critical++;}
void AppTest_ExitCritical(void)
{
    RuntimeEventTarget_t previous=s_app_owner;
    g_app_critical--;
    if(s_defer_collect && g_app_critical==0U)
    {
        s_defer_collect=0U;s_app_owner=RUNTIME_TARGET_IPC;
        if(App_StatusGetSnapshot(&s_deferred_snapshot)!=SYS_OK)s_interleave_fail=__LINE__;
        s_app_owner=previous;
    }
}
#if defined(CORE_CM7)
uint32_t Platform_TimeGetMs(void){return s_now;}
#else
//CM4的时间桩来自旧协议测试，CM7 Application测试也使用同一个受控值
#define s_now g_test_tick
#endif

static bool Owner(void *context){(void)context;return s_owner && s_thread && s_mask==0U;}

//构造合法完整载荷，各字段随编号变化，便于识别半更新
static IpcApplicationStatus_t Make(uint32_t id)
{
    IpcApplicationStatus_t a={0};
    a.valid_mask=IPC_APP_ALL;a.camera_state=1U;a.usb_state=1U;a.control_state=3U;
    a.camera_fps_milli=15000U+id;a.usb_fps_milli=16000U+id;
    a.captured_frames=1000U+id;a.dropped_frames=id;a.usb_completed_frames=900U+id;
    a.width=id==1U?160U:320U;a.height=id==1U?120U:240U;a.format=IPC_IMAGE_JPEG;
    a.last_fault=SYS_ERR_IO;a.camera_result=SYS_OK;a.usb_result=SYS_ERR_BUSY;
    a.control_result=SYS_ERR_IO;a.camera_age_ms=10U+id;a.usb_age_ms=20U+id;
    a.control_age_ms=30U+id;a.snapshot_id=id;return a;
}

static bool Equal(const IpcApplicationStatus_t *a,const IpcApplicationStatus_t *b)
{
    return a->valid_mask==b->valid_mask && a->camera_state==b->camera_state &&
    a->usb_state==b->usb_state && a->control_state==b->control_state &&
    a->camera_fps_milli==b->camera_fps_milli && a->usb_fps_milli==b->usb_fps_milli &&
    a->captured_frames==b->captured_frames && a->dropped_frames==b->dropped_frames &&
    a->usb_completed_frames==b->usb_completed_frames && a->width==b->width &&
    a->height==b->height && a->format==b->format && a->last_fault==b->last_fault &&
    a->camera_result==b->camera_result && a->usb_result==b->usb_result &&
    a->control_result==b->control_result && a->camera_age_ms==b->camera_age_ms &&
    a->usb_age_ms==b->usb_age_ms && a->control_age_ms==b->control_age_ms && a->snapshot_id==b->snapshot_id;
}

//CM4只模拟对端已提交，不提供CM4写CM7业务的生产接口
void SeedApplication(const IpcApplicationStatus_t *a)
{
    uint32_t sequence=Peer()->sequence;
    Peer()->sequence=sequence+1U;
    Peer()->application=*a;
    Peer()->sequence=sequence+2U;
}

static SysResult_t Publish(const IpcApplicationStatus_t *a)
{
#if defined(CORE_CM7)
    IpcState_t state={IPC_CORE_READY,100U,0U,SYS_OK};
    return IPC_Core_PublishRuntimeApplication(&state,a);
#else
    SeedApplication(a);return SYS_OK;
#endif
}
static SysResult_t ReadApplication(IpcStatusSnapshot_t *value)
{
#if defined(CORE_CM7)
    return IPC_Core_ReadLocal(value);
#else
    return IPC_Core_ReadPeer(value);
#endif
}
static int Prepare(void)
{
    CHECK(Setup()==0);
    CHECK(IPC_Core_ConfigureRuntime(Owner,NULL)==SYS_OK);
    CHECK(IPC_Core_EndInitialization()==SYS_OK);return 0;
}

//在每个真实字段读写点插入竞争，不复制生产序号算法
void IPC_TestAfterAccess(uint32_t operation,uint32_t word)
{
#if defined(CORE_CM7)
    IpcStatusSnapshot_t snapshot;
#endif
    Ipc_PreviousAccessHook(operation,word);
    if(s_interleave_busy || word!=s_interleave_word)return;
    s_interleave_busy=true;
#if defined(CORE_CM7)
    if(s_interleave_mode==1U && operation==1U)
    {
        Fill(&snapshot,0xCDU,sizeof(snapshot));
        if(ReadApplication(&snapshot)!=SYS_ERR_BUSY || !Filled(&snapshot,0xCDU,sizeof(snapshot)))s_interleave_fail=__LINE__;
    }
#endif
    if(s_interleave_mode==2U && operation==3U)
    {
        s_interleave_mode=0U;
        if(Publish(&s_next)!=SYS_OK)s_interleave_fail=__LINE__;
    }
    s_interleave_busy=false;
}

//验证Application参数、所有权及跨来源一致快照
static int ApplicationCase(uint32_t id)
{
    AppCameraStatus_t camera={1U,15000U,100U,2U,160U,120U,CAMERA_PIXEL_FORMAT_JPEG,true,SYS_OK};
    AppUsbStatus_t usb={1U,16000U,90U,SYS_OK};
    AppControlStatus_t control={3U,SYS_OK,SYS_ERR_IO};
    IpcApplicationStatus_t a,b;
    if(id==40U){CHECK(App_StatusGetSnapshot(NULL)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==41U){CHECK(App_StatusGetSnapshot((IpcApplicationStatus_t *)((uintptr_t)&g_ipc+512U))==SYS_ERR_INVALID_ARG);return 0;}
    if(id==42U){CHECK(App_StatusGetSnapshot(&a)==SYS_OK);CHECK(a.valid_mask==0U);return 0;}
    s_app_owner=RUNTIME_TARGET_USB;
    if(id==43U){CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_STATE);return 0;}
    s_app_owner=RUNTIME_TARGET_CAMERA;
    if(id==44U){camera.format=(CameraPixelFormat_t)99;CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==45U){camera.width=65536U;CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==46U){camera.format=CAMERA_PIXEL_FORMAT_YUV422;camera.width=161U;CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==47U){camera.config_valid=false;CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==48U){camera.captured_frames=UINT32_MAX;camera.dropped_frames=UINT32_MAX;}
    if(id==49U)s_defer_collect=1U;
    s_now=0xFFFFFFF0UL;
    CHECK(App_StatusPublishCamera(&camera)==SYS_OK);
    if(id==49U){CHECK(s_deferred_snapshot.captured_frames==100U && s_deferred_snapshot.width==160U && s_interleave_fail==0U);return 0;}
    s_app_owner=RUNTIME_TARGET_USB;CHECK(App_StatusPublishUsb(&usb)==SYS_OK);
    s_app_owner=RUNTIME_TARGET_CONTROL;CHECK(App_StatusPublishControl(&control)==SYS_OK);
    s_app_owner=RUNTIME_TARGET_IPC;s_now=0x20U;CHECK(App_StatusGetSnapshot(&a)==SYS_OK);
    CHECK(a.valid_mask==IPC_APP_ALL && a.camera_age_ms==48U && a.usb_age_ms==48U);
    CHECK(a.camera_fps_milli==15000U && a.usb_fps_milli==16000U && a.last_fault==SYS_ERR_IO);
    if(id==48U)CHECK(a.captured_frames==UINT32_MAX && a.dropped_frames==UINT32_MAX);
    if(id==50U)
    {
        s_now=0x200U;CHECK(App_StatusGetSnapshot(&a)==SYS_OK);CHECK(a.camera_age_ms==UINT32_MAX);
        s_now=0x10U;CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.camera_age_ms==UINT32_MAX);
    }
    if(id==51U)
    {
        s_now=0x200U;CHECK(App_StatusGetSnapshot(&a)==SYS_OK);
        s_app_owner=RUNTIME_TARGET_CAMERA;camera.captured_frames=0U;CHECK(App_StatusPublishCamera(&camera)==SYS_OK);
        s_app_owner=RUNTIME_TARGET_IPC;CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.camera_age_ms==0U && b.captured_frames==0U);
    }
    if(id==52U){CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.snapshot_id==a.snapshot_id+1U);}
    if(id==53U){s_thread=false;CHECK(App_StatusGetSnapshot(&b)==SYS_ERR_INVALID_STATE);}
    if(id==54U){s_app_owner=RUNTIME_TARGET_CAMERA;camera.result=1;CHECK(App_StatusPublishCamera(&camera)==SYS_ERR_INVALID_ARG);}
    if(id==55U){s_app_owner=RUNTIME_TARGET_USB;usb.result=1;CHECK(App_StatusPublishUsb(&usb)==SYS_ERR_INVALID_ARG);}
    if(id==56U){s_app_owner=RUNTIME_TARGET_CONTROL;control.state=7U;CHECK(App_StatusPublishControl(&control)==SYS_ERR_INVALID_ARG);}
    if(id==57U){s_app_owner=RUNTIME_TARGET_CONTROL;control.last_fault=1;CHECK(App_StatusPublishControl(&control)==SYS_ERR_INVALID_ARG);}
    if(id==58U)CHECK(App_StatusGetSnapshot((IpcApplicationStatus_t *)0xFFFFFFF0UL)==SYS_ERR_INVALID_ARG);
    if(id==59U)CHECK(App_StatusGetSnapshot((IpcApplicationStatus_t *)((uintptr_t)&b+1U))==SYS_ERR_INVALID_ARG);
    if(id==60U){s_app_owner=RUNTIME_TARGET_CAMERA;camera.state=0U;camera.config_valid=false;CHECK(App_StatusPublishCamera(&camera)==SYS_OK);s_app_owner=RUNTIME_TARGET_IPC;CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.width==0U && b.format==IPC_IMAGE_UNKNOWN);}
    if(id==61U){s_app_owner=RUNTIME_TARGET_USB;usb.completed_frames=UINT32_MAX;CHECK(App_StatusPublishUsb(&usb)==SYS_OK);usb.completed_frames=0U;CHECK(App_StatusPublishUsb(&usb)==SYS_OK);s_app_owner=RUNTIME_TARGET_IPC;CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.usb_completed_frames==0U);}
    if(id==62U){s_app_owner=RUNTIME_TARGET_CAMERA;camera.format=CAMERA_PIXEL_FORMAT_RGB565;CHECK(App_StatusPublishCamera(&camera)==SYS_OK);s_app_owner=RUNTIME_TARGET_IPC;CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.format==IPC_IMAGE_RGB565);}
    if(id==63U){s_app_owner=RUNTIME_TARGET_CAMERA;camera.format=CAMERA_PIXEL_FORMAT_Y8;CHECK(App_StatusPublishCamera(&camera)==SYS_OK);s_app_owner=RUNTIME_TARGET_IPC;CHECK(App_StatusGetSnapshot(&b)==SYS_OK);CHECK(b.format==IPC_IMAGE_Y8);}
    return 0;
}

int IpcStatus_TestRun(uint32_t id)
{
    IpcApplicationStatus_t a=Make(1U),b=Make(2U);
    IpcStatusSnapshot_t snapshot;
    IpcApplicationCache_t cache={0};
    IpcApplicationView_t view;
#if defined(CORE_CM7)
    uint32_t before;
#endif
    if(id>=40U && id<64U)return ApplicationCase(id);
    if(id>=100U)
    {
        CHECK(Prepare()==0);CHECK(Publish(&a)==SYS_OK);s_next=b;
        s_interleave_word=(id>=200U?id-200U:id-100U);
#if defined(CORE_CM7)
        if(id<200U)
        {
            s_interleave_mode=1U;CHECK(Publish(&b)==SYS_OK);s_interleave_mode=0U;
            CHECK(s_interleave_fail==0U);CHECK(ReadApplication(&snapshot)==SYS_OK);CHECK(Equal(&snapshot.application,&b));return 0;
        }
#endif
        s_interleave_mode=2U;CHECK(ReadApplication(&snapshot)==SYS_OK);
        CHECK(s_interleave_fail==0U && Equal(&snapshot.application,&b));return 0;
    }
    if(id==0U){CHECK(sizeof(g_ipc)==320U && sizeof(a)==80U);CHECK(offsetof(IpcStatusBlock_t,application)==28U && offsetof(IpcStatusBlock_t,reserved)==108U);CHECK(IPC_VERSION==3U);return 0;}
    if(id<=9U)
    {
        CHECK(Prepare()==0);
        if(id==1U || id==2U){g_ipc.header.version=id;Fill(&snapshot,0xCDU,sizeof(snapshot));CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_PROTOCOL);CHECK(Filled(&snapshot,0xCDU,sizeof(snapshot)));return 0;}
        if(id==3U){CHECK(Publish(&a)==SYS_OK);CHECK(ReadApplication(&snapshot)==SYS_OK);CHECK(Equal(&snapshot.application,&a));return 0;}
#if defined(CORE_CM7)
        if(id==4U){s_owner=false;before=Digest(&g_ipc,sizeof(g_ipc));CHECK(Publish(&a)==SYS_ERR_INVALID_STATE);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));return 0;}
        if(id==5U){a.valid_mask=0x80U;before=Digest(&g_ipc,sizeof(g_ipc));CHECK(Publish(&a)==SYS_ERR_INVALID_ARG);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));return 0;}
#else
        if(id==4U || id==5U){Local()->application=a;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_PROTOCOL);return 0;}
#endif
        if(id==6U){Peer()->application.valid_mask=0x80U;Fill(&snapshot,0xCDU,sizeof(snapshot));CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_PROTOCOL);CHECK(Filled(&snapshot,0xCDU,sizeof(snapshot)));return 0;}
        if(id==7U){g_ipc.header.feature_flags=IPC_FEATURE_SEQLOCK;CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_PROTOCOL);return 0;}
        if(id==8U){Peer()->reserved[4]=1U;CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_PROTOCOL);return 0;}
        if(id==9U){Peer()->sequence=3U;CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_BUSY);return 0;}
    }
    switch(id)
    {
      case 10:a.width=65536U;CHECK(!IPC_Application_Validate(&a));return 0;
      case 11:a.width=65535U;a.height=65535U;CHECK(IPC_Application_Validate(&a));return 0;
      case 12:a.format=IPC_IMAGE_YUY2;a.width=161U;CHECK(!IPC_Application_Validate(&a));return 0;
      case 13:a.format=99U;CHECK(!IPC_Application_Validate(&a));return 0;
      case 14:a.camera_result=1;CHECK(!IPC_Application_Validate(&a));return 0;
      case 15:a.last_fault=1;CHECK(!IPC_Application_Validate(&a));return 0;
      case 16:a.camera_fps_milli=UINT32_MAX;a.usb_fps_milli=UINT32_MAX;a.captured_frames=UINT32_MAX;a.dropped_frames=UINT32_MAX;a.usb_completed_frames=UINT32_MAX;CHECK(IPC_Application_Validate(&a));return 0;
      case 17:a.valid_mask=0U;CHECK(!IPC_Application_Validate(&a));return 0;
      case 18:CHECK(IPC_Application_GetView(&cache,0U,IPC_PEER_ONLINE,SYS_OK,&view)==SYS_OK);CHECK(view.valid_mask==0U && view.fresh_mask==0U && view.receive_age_ms==UINT32_MAX);return 0;
      case 19:CHECK(IPC_Application_GetView(NULL,0U,1U,SYS_OK,&view)==SYS_ERR_INVALID_ARG);return 0;
      default:break;
    }
    IPC_Application_Observe(&cache,&a,0xFFFFFFF0UL);
    CHECK(IPC_Application_GetView(&cache,0x20U,IPC_PEER_ONLINE,SYS_OK,&view)==SYS_OK);
    CHECK(view.valid_mask==IPC_APP_ALL && view.fresh_mask==IPC_APP_ALL && view.receive_age_ms==48U);
    if(id==20U)return 0;
    if(id==21U){CHECK(IPC_Application_GetView(&cache,0x1E4U,IPC_PEER_ONLINE,SYS_OK,&view)==SYS_OK);CHECK(view.fresh_mask==0U);}
    if(id==22U){IPC_Application_Observe(&cache,&a,0x200U);CHECK(cache.received_tick==0xFFFFFFF0UL && cache.expired);CHECK(IPC_Application_GetView(&cache,0x10U,IPC_PEER_ONLINE,SYS_OK,&view)==SYS_OK);CHECK(view.fresh_mask==0U);}
    if(id==23U){a.camera_age_ms=UINT32_MAX;a.snapshot_id=3U;IPC_Application_Observe(&cache,&a,0x20U);CHECK(IPC_Application_GetView(&cache,0x21U,IPC_PEER_ONLINE,SYS_OK,&view)==SYS_OK);CHECK(view.application.camera_age_ms==UINT32_MAX && (view.fresh_mask&IPC_APP_CAMERA)==0U);}
    if(id==24U){CHECK(IPC_Application_GetView(&cache,0x20U,IPC_PEER_TIMEOUT,SYS_OK,&view)==SYS_OK);CHECK(view.fresh_mask==0U && view.valid_mask==IPC_APP_ALL);}
    if(id==25U){CHECK(IPC_Application_GetView(&cache,0x20U,IPC_PEER_ONLINE,SYS_ERR_PROTOCOL,&view)==SYS_OK);CHECK(view.fresh_mask==0U);}
    if(id==26U){CHECK(IPC_Application_GetView(&cache,0x20U,IPC_PEER_ONLINE,SYS_ERR_BUSY,&view)==SYS_OK);CHECK(view.fresh_mask==IPC_APP_ALL);}
    if(id==27U){a.snapshot_id=UINT32_MAX;IPC_Application_Observe(&cache,&a,0x20U);a.snapshot_id=0U;IPC_Application_Observe(&cache,&a,0x30U);CHECK(cache.received_tick==0x30U && cache.application.snapshot_id==0U);}
    if(id==28U){a.camera_age_ms=500U;a.snapshot_id=3U;IPC_Application_Observe(&cache,&a,0x20U);CHECK(IPC_Application_GetView(&cache,0x20U,IPC_PEER_ONLINE,SYS_OK,&view)==SYS_OK);CHECK(view.fresh_mask==(IPC_APP_USB|IPC_APP_CONTROL));}
    if(id==29U){CHECK(IPC_Application_GetView(&cache,0x20U,IPC_PEER_STARTING,SYS_OK,&view)==SYS_OK);CHECK(view.fresh_mask==0U);}
    if(id==30U){b.valid_mask=0x80U;IPC_Application_Observe(&cache,&b,0x40U);CHECK(cache.application.snapshot_id==1U);}
    if(id==31U){IPC_Application_Observe(&cache,&b,0x200U);CHECK(!cache.expired && cache.application.snapshot_id==2U);}
    if(id>=32U && id<=39U)
    {
#if defined(CORE_CM7)
        CHECK(IPC_Service_SetRuntimeOwner(Owner,NULL)==SYS_OK);CHECK(IPC_Service_EarlyInit()==SYS_OK);CHECK(IPC_Service_Init()==SYS_OK);
        CHECK(IPC_Service_SetApplication(&a,0U)==SYS_ERR_INVALID_STATE);CHECK(IPC_Service_RuntimeStart(0U)==SYS_OK);
        if(id==33U){s_owner=false;CHECK(IPC_Service_SetApplication(&a,0U)==SYS_ERR_INVALID_STATE);return 0;}
        if(id==34U){CHECK(IPC_Service_SetApplication(NULL,0U)==SYS_ERR_INVALID_ARG);return 0;}
        if(id==35U){CHECK(IPC_Service_SetApplication((IpcApplicationStatus_t *)((uintptr_t)&g_ipc+512U),0U)==SYS_ERR_INVALID_ARG);return 0;}
        if(id==36U)
        {
            CHECK(IPC_Service_SetApplication(&a,0U)==SYS_OK);CHECK(IPC_Service_RuntimeProcess(600U)==SYS_OK);
            a.camera_age_ms+=600U;a.usb_age_ms+=600U;a.control_age_ms+=600U;
        }
        else if(id==37U)
        {
            CHECK(IPC_Service_SetApplication(&a,0xFFFFFFF0UL)==SYS_OK);CHECK(IPC_Service_RuntimeProcess(0x20U)==SYS_OK);
            a.camera_age_ms+=48U;a.usb_age_ms+=48U;a.control_age_ms+=48U;
        }
        else if(id==38U)
        {
            a.camera_age_ms=UINT32_MAX-2U;CHECK(IPC_Service_SetApplication(&a,0U)==SYS_OK);
            CHECK(IPC_Service_RuntimeProcess(10U)==SYS_OK);
            a.camera_age_ms=UINT32_MAX;a.usb_age_ms+=10U;a.control_age_ms+=10U;
        }
        else
        {
            CHECK(IPC_Service_SetApplication(&a,0U)==SYS_OK);CHECK(IPC_Service_RuntimeProcess(0U)==SYS_OK);
        }
        CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK && Equal(&snapshot.application,&a));
#else
        SeedHeader();SeedPeer(2U);CHECK(IPC_Service_Init()==SYS_OK);
        CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_OK && view.valid_mask==0U && view.fresh_mask==0U);
        if(id==33U){CHECK(IPC_Service_GetApplicationStatus(NULL)==SYS_ERR_INVALID_ARG);return 0;}
        if(id==34U){CHECK(IPC_Service_GetApplicationStatus((IpcApplicationView_t *)((uintptr_t)&g_ipc+512U))==SYS_ERR_INVALID_ARG);return 0;}
        if(id==35U){s_thread=false;CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_ERR_INVALID_STATE);return 0;}
        SeedApplication(&a);g_test_tick=100U;g_test_timer_config.callback(NULL);
        CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_OK && Equal(&view.application,&a));
        CHECK(view.fresh_mask==0U);
        Peer()->heartbeat++;g_test_tick=200U;g_test_timer_config.callback(NULL);
        CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_OK && view.fresh_mask==IPC_APP_ALL);
        if(id==36U){g_test_tick=600U;CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_OK && view.fresh_mask==0U);}
        if(id==37U){g_ipc.header.version=2U;g_test_tick=300U;g_test_timer_config.callback(NULL);CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_OK && view.fresh_mask==0U && view.read_result==SYS_ERR_PROTOCOL);}
        if(id==38U){Peer()->sequence|=1U;g_test_tick=300U;g_test_timer_config.callback(NULL);CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_OK && view.fresh_mask==IPC_APP_ALL && view.read_result==SYS_ERR_BUSY);}
        if(id==39U){g_test_tick=0x10U;CHECK(IPC_Service_GetApplicationStatus(&view)==SYS_OK && view.fresh_mask==0U);}
#endif
    }
    CHECK(s_interleave_fail==0U);return 0;
}
