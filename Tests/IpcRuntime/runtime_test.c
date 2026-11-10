#define Ipc_TestRun IpcV2_TestRun
#include "../IpcV2/protocol_test.c"
#undef Ipc_TestRun
#include "ipc_runtime.h"

//只在独立模拟用例中决定授权者身份
static bool s_owner_allowed = true;

//保存单例本地状态机，实际代码来自Common/Src/ipc_runtime.c
static IpcRuntime_t s_test_runtime;

//模拟Application身份判断，不复制Core授权逻辑
static bool TestOwner(void *context)
{
    (void)context;
    return s_owner_allowed && s_thread && s_mask == 0U;
}

//模拟对端在两次读之间提交心跳
static void PeerHeartbeat(uint32_t heartbeat)
{
    uint32_t sequence = Peer()->sequence;
    Peer()->sequence = sequence + 1U;
    Peer()->heartbeat = heartbeat;
    Peer()->sequence = sequence + 2U;
}

//准备真实Core的运行期权限
static int RuntimePrepare(uint32_t now)
{
    CHECK(Setup() == 0);
    CHECK(IPC_Core_ConfigureRuntime(TestOwner, NULL) == SYS_OK);
    CHECK(IPC_Core_EndInitialization() == SYS_OK);
    CHECK(IPC_Runtime_Start(&s_test_runtime, now) == SYS_OK);
    return 0;
}

//启动观察并取得一次后续心跳进展
static int Online(void)
{
    CHECK(RuntimePrepare(0U) == 0);
    CHECK(IPC_Runtime_Process(&s_test_runtime, 0U) == SYS_OK);
    PeerHeartbeat(18U);
    CHECK(IPC_Runtime_Process(&s_test_runtime, 100U) == SYS_OK);
    CHECK(s_test_runtime.status.peer_state == IPC_PEER_ONLINE);
    return 0;
}

//执行真实Service的授权、Timer及失败路径
static int ServiceTest(uint32_t id)
{
    IpcRuntimeStatus_t status;
    IpcState_t state = {IPC_CORE_READY, 100U, 0U, SYS_OK};
#if defined(CORE_CM7)
    CHECK(IPC_Service_SetRuntimeOwner(NULL, NULL) == SYS_ERR_INVALID_ARG);
    CHECK(IPC_Service_SetRuntimeOwner(TestOwner, (void *)((uintptr_t)&g_ipc+512U)) == SYS_ERR_INVALID_ARG);
    s_thread=false;
    CHECK(IPC_Service_SetRuntimeOwner(TestOwner, NULL) == SYS_ERR_INVALID_STATE);
    s_thread=true;
    CHECK(IPC_Service_SetRuntimeOwner(TestOwner, NULL) == SYS_OK);
    CHECK(IPC_Service_SetRuntimeOwner(TestOwner, NULL) == SYS_ERR_INVALID_STATE);
    CHECK(IPC_Service_EarlyInit() == SYS_OK);
    CHECK(IPC_Service_Init() == SYS_OK);
    if (id == 50U)
    {
        CHECK(IPC_Service_RuntimeStart(500U) == SYS_OK);
        CHECK(IPC_Service_RuntimeProcess(500U) == SYS_OK);
        CHECK(Local()->heartbeat == 1U);return 0;
    }
    if (id == 51U)
    {
        s_owner_allowed = false;
        CHECK(IPC_Service_RuntimeStart(0U) == SYS_ERR_INVALID_STATE);
        CHECK(IPC_Service_RuntimeProcess(0U) == SYS_ERR_INVALID_STATE);
        CHECK(Local()->heartbeat == 0U);return 0;
    }
    CHECK(IPC_Service_RuntimeStart(0U) == SYS_OK);
    if (id == 52U)CHECK(IPC_Service_RuntimeStart(0U) == SYS_ERR_ALREADY_EXISTS);
    SeedPeer(2U);
    CHECK(IPC_Service_RuntimeProcess(0U) == SYS_OK);
    if (id == 53U)CHECK(IPC_Service_SetRuntimeOwner(TestOwner, NULL) == SYS_ERR_INVALID_STATE);
    if (id == 54U)
    {
        s_owner_allowed = false;
        CHECK(IPC_Service_RuntimeProcess(100U) == SYS_ERR_INVALID_STATE);
        CHECK(Local()->heartbeat == 1U);return 0;
    }
    if (id == 55U)
    {
        PeerHeartbeat(18U);
        CHECK(IPC_Service_RuntimeProcess(100U) == SYS_OK);
        CHECK(g_ipc_runtime_debug.peer_state == IPC_PEER_ONLINE);
    }
#else
    SeedHeader();SeedPeer(2U);
    if (id == 52U)g_test_create_result = SYS_ERR_NO_RESOURCE;
    if (id == 53U)g_test_start_result = SYS_ERR_IO;
    if (id == 52U || id == 53U)
    {
        CHECK(IPC_Service_Init() == (id == 52U ? SYS_ERR_NO_RESOURCE : SYS_ERR_IO));
        CHECK(g_test_timer_creates == 1U);
        CHECK(!s_service_initialized);
        CHECK(g_test_timer_deletes == (id == 53U ? 1U : 0U));return 0;
    }
    CHECK(IPC_Service_Init() == SYS_OK);
    CHECK(g_test_timer_creates == 1U && g_test_timer_starts == 1U);
    CHECK(g_test_timer_config.period_ms == 100U);
    CHECK(g_test_timer_config.mode == FRAMEWORK_TIMER_PERIODIC);
    CHECK(IPC_Core_PublishRuntime(&state) == SYS_ERR_INVALID_STATE);
    if (id == 51U)CHECK(!s_timer_executing);
    g_test_tick = 100U;
    g_test_timer_config.callback(g_test_timer_config.context);
    CHECK(!s_timer_executing && Local()->heartbeat == 1U);
    if (id == 54U)
    {
        g_test_tick = 199U;
        g_test_timer_config.callback(NULL);
        CHECK(Local()->heartbeat == 1U);
    }
    if (id == 55U)
    {
        PeerHeartbeat(18U);g_test_tick = 200U;
        g_test_timer_config.callback(NULL);
        CHECK(g_ipc_runtime_debug.peer_state == IPC_PEER_ONLINE);
    }
#endif
    if (id == 56U)CHECK(IPC_Core_PublishLocal(&state) == SYS_ERR_INVALID_STATE);
    if (id == 57U)CHECK(IPC_Core_ConfigureRuntime(TestOwner,NULL) == SYS_ERR_INVALID_STATE);
    if (id == 58U)CHECK(IPC_Service_GetRuntimeStatus(NULL) == SYS_ERR_INVALID_ARG);
    if (id == 59U)CHECK(IPC_Service_GetRuntimeStatus((IpcRuntimeStatus_t *)((uintptr_t)&g_ipc+512U)) == SYS_ERR_INVALID_ARG);
    if (id == 60U)
    {
        s_thread=false;CHECK(IPC_Service_GetRuntimeStatus(&status)==SYS_ERR_INVALID_STATE);return 0;
    }
    if (id == 61U)CHECK(IPC_Service_GetRuntimeStatus((IpcRuntimeStatus_t *)0xFFFFFFF0UL)==SYS_ERR_INVALID_ARG);
    if (id == 62U)CHECK(IPC_Service_GetRuntimeStatus((IpcRuntimeStatus_t *)((uintptr_t)&status+1U))==SYS_ERR_INVALID_ARG);
    CHECK(IPC_Service_GetRuntimeStatus(&status)==SYS_OK);
    CHECK(status.local_heartbeat==g_ipc_runtime_debug.local_heartbeat);
    return 0;
}

//每个编号在独立模拟RAM中执行，失败返回真实断言行
int IpcRuntime_TestRun(uint32_t id)
{
    IpcState_t state={IPC_CORE_READY, 123U, 0U, SYS_OK};
    uint32_t before;
    if(id==64U)
    {
        CHECK(RuntimePrepare(0U)==0);
        CHECK(IPC_Runtime_Process(&s_test_runtime,1000U)==SYS_OK);
        CHECK(s_test_runtime.status.max_step_gap_ms==1000U);
        CHECK(s_test_runtime.status.scheduling_delays==1U);
        CHECK(s_test_runtime.status.local_heartbeat==1U);
        CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTUP_TIMEOUT);return 0;
    }
    if(id>=50U)return ServiceTest(id);
    if(id<=7U || id==47U)
    {
        CHECK(Setup()==0);
        if(id==47U)
        {
            CHECK(IPC_Core_ConfigureRuntime(TestOwner,(void *)((uintptr_t)&g_ipc+512U))==SYS_ERR_INVALID_ARG);
            return 0;
        }
        if(id==0U)
        {
            CHECK(IPC_Core_EndInitialization()==SYS_OK);
            CHECK(IPC_Core_PublishRuntime(&state)==SYS_ERR_INVALID_STATE);return 0;
        }
        CHECK(IPC_Core_ConfigureRuntime(NULL,NULL)==SYS_ERR_INVALID_ARG);
        CHECK(IPC_Core_ConfigureRuntime(TestOwner,NULL)==SYS_OK);
        CHECK(IPC_Core_ConfigureRuntime(TestOwner,NULL)==SYS_ERR_ALREADY_EXISTS);
        if(id==2U)CHECK(IPC_Core_PublishRuntime(&state)==SYS_ERR_INVALID_STATE);
        CHECK(IPC_Core_EndInitialization()==SYS_OK);
        before=Digest(&g_ipc,sizeof(g_ipc));
        if(id==1U){CHECK(IPC_Core_PublishRuntime(&state)==SYS_OK);CHECK(Local()->heartbeat==123U);}
        if(id==2U || id==47U)CHECK(IPC_Core_ConfigureRuntime(TestOwner,NULL)==SYS_ERR_INVALID_STATE);
        if(id==3U){s_owner_allowed=false;CHECK(IPC_Core_PublishRuntime(&state)==SYS_ERR_INVALID_STATE);}
        if(id==4U){s_thread=false;CHECK(IPC_Core_PublishRuntime(&state)==SYS_ERR_INVALID_STATE);}
        if(id==5U){g_ipc.header.version=1U;before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_PublishRuntime(&state)==SYS_ERR_PROTOCOL);}
        if(id==6U)CHECK(IPC_Core_PublishRuntime((const IpcState_t *)((uintptr_t)&g_ipc+512U))==SYS_ERR_INVALID_ARG);
        if(id==7U){Local()->sequence=0xFFFFFFFEUL;before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_PublishRuntime(&state)==SYS_ERR_NO_RESOURCE);}
        if(id!=1U)CHECK(Digest(&g_ipc,sizeof(g_ipc))==before);return 0;
    }
    if(id==8U) {CHECK(IPC_Service_GetRuntimeStatus((IpcRuntimeStatus_t *)&g_ipc)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==9U) {CHECK(IPC_Runtime_Start(NULL,0U)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==10U){CHECK(IPC_Runtime_Process(NULL,0U)==SYS_ERR_INVALID_ARG);return 0;}
    if(id==11U){CHECK(IPC_Runtime_Process(&s_test_runtime,0U)==SYS_ERR_INVALID_STATE);return 0;}
    if(id==26U || id==48U)
    {
        CHECK(RuntimePrepare(0xFFFFFF00UL)==0);
        CHECK(IPC_Runtime_Process(&s_test_runtime,0xFFFFFF00UL)==SYS_OK);
        CHECK(IPC_Runtime_Process(&s_test_runtime,0xFFFFFF63UL)==SYS_OK);
        CHECK(s_test_runtime.status.local_heartbeat==1U);
        PeerHeartbeat(18U);
        CHECK(IPC_Runtime_Process(&s_test_runtime,0xFFFFFF64UL)==SYS_OK);
        CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE);
        CHECK(IPC_Runtime_Process(&s_test_runtime,(uint32_t)(0xFFFFFF64UL + IPC_HEARTBEAT_TIMEOUT_MS))==SYS_OK);
        CHECK(s_test_runtime.status.peer_state==IPC_PEER_TIMEOUT);
        CHECK(s_test_runtime.status.peer_age_ms==IPC_HEARTBEAT_TIMEOUT_MS);return 0;
    }
    if(id==18U || id==20U || id==21U || id==23U || id==27U || id==28U || id==32U || id==33U || id==34U || id==38U)
        CHECK(Online()==0);
    else
    {
        CHECK(RuntimePrepare(0U)==0);
        if(id==12U){CHECK(IPC_Runtime_Start(&s_test_runtime,1U)==SYS_ERR_ALREADY_EXISTS);return 0;}
        CHECK(IPC_Runtime_Process(&s_test_runtime,0U)==SYS_OK);
    }
    switch(id)
    {
      case 13:
        CHECK(IPC_Runtime_Process(&s_test_runtime,99U)==SYS_OK);CHECK(Local()->heartbeat==1U);
        CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);CHECK(Local()->heartbeat==2U);break;
      case 14:CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTING && !s_test_runtime.status.progress_seen);CHECK(s_test_runtime.status.max_step_gap_ms==0U);break;
      case 15:PeerHeartbeat(18U);CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE);break;
      case 16:CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS - IPC_HEARTBEAT_PERIOD_MS)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTING);break;
      case 17:CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS - IPC_HEARTBEAT_PERIOD_MS)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTING);CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTUP_TIMEOUT);break;
      case 18:CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE);CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 100U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_TIMEOUT);break;
      case 19:s_test_runtime.sampled=false;CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS - IPC_HEARTBEAT_PERIOD_MS)==SYS_OK);CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTUP_TIMEOUT);break;
      case 20:Peer()->sequence=3U;CHECK(IPC_Runtime_Process(&s_test_runtime,200U)==SYS_OK);CHECK(s_test_runtime.status.busy_reads==1U && s_test_runtime.status.peer_state==IPC_PEER_ONLINE);break;
      case 21:Peer()->sequence=3U;CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 100U)==SYS_OK);CHECK(s_test_runtime.status.busy_reads==1U && s_test_runtime.status.peer_state==IPC_PEER_TIMEOUT);break;
      case 22:g_ipc.header.version=1U;CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_ERR_PROTOCOL);CHECK(s_test_runtime.status.peer_state==IPC_PEER_PROTOCOL_ERROR && s_test_runtime.status.protocol_errors==1U && !s_test_runtime.timed_out);break;
      case 23:g_ipc.header.version=1U;CHECK(IPC_Runtime_Process(&s_test_runtime,200U)==SYS_ERR_PROTOCOL);g_ipc.header.version=IPC_VERSION;PeerHeartbeat(19U);CHECK(IPC_Runtime_Process(&s_test_runtime,300U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE && s_test_runtime.status.protocol_errors==1U);break;
      case 24:PeerHeartbeat(UINT32_MAX);CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);PeerHeartbeat(0U);CHECK(IPC_Runtime_Process(&s_test_runtime,200U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE && s_test_runtime.status.peer_heartbeat==0U);break;
      case 25:s_test_runtime.status.local_heartbeat=UINT32_MAX;CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);CHECK(Local()->heartbeat==0U && s_test_runtime.status.local_heartbeat==0U);break;
      case 27:PeerHeartbeat(1000U);CHECK(IPC_Runtime_Process(&s_test_runtime,1100U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE && s_test_runtime.status.local_heartbeat==3U && s_test_runtime.status.scheduling_delays==1U);break;
      case 28:CHECK(IPC_Runtime_Process(&s_test_runtime,1100U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_TIMEOUT && s_test_runtime.status.local_heartbeat==3U);break;
      case 29:CHECK(IPC_Runtime_Process(&s_test_runtime,0U)==SYS_OK);CHECK(Local()->heartbeat==1U);break;
      case 30:Peer()->ready=0U;PeerHeartbeat(18U);CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTING);break;
      case 31:Peer()->ready=0U;CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);Peer()->ready=1U;CHECK(IPC_Runtime_Process(&s_test_runtime,200U)==SYS_OK);CHECK(!s_test_runtime.status.progress_seen);break;
      case 32:CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 100U)==SYS_OK);CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 200U)==SYS_OK);CHECK(s_test_runtime.status.timeout_events==1U && s_test_runtime.status.startup_timeouts==0U);break;
      case 33:CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 100U)==SYS_OK);PeerHeartbeat(19U);CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 200U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE && (Local()->fault_flags&IPC_FAULT_HEARTBEAT_TIMEOUT)!=0U);break;
      case 34:g_ipc.header.version=99U;CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 100U)==SYS_ERR_PROTOCOL);CHECK(s_test_runtime.status.protocol_errors==1U && s_test_runtime.status.timeout_events==1U && s_test_runtime.status.peer_state==IPC_PEER_PROTOCOL_ERROR);break;
      case 35:Local()->sequence=0xFFFFFFFEUL;CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_ERR_NO_RESOURCE);CHECK(s_test_runtime.status.local_heartbeat==1U);break;
      case 36:s_owner_allowed=false;before=Digest(Local(),sizeof(*Local()));CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_ERR_INVALID_STATE);CHECK(Digest(Local(),sizeof(*Local()))==before && s_test_runtime.status.local_heartbeat==1U);break;
      case 37:Peer()->sequence=3U;CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);SeedPeer(4U);g_ipc.header.version=99U;CHECK(IPC_Runtime_Process(&s_test_runtime,200U)==SYS_ERR_PROTOCOL);CHECK(s_test_runtime.status.busy_reads==1U && s_test_runtime.status.protocol_errors==1U);break;
      case 38:CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 100U)==SYS_OK);PeerHeartbeat(19U);CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS + 200U)==SYS_OK);CHECK(IPC_Runtime_Process(&s_test_runtime,2U * IPC_HEARTBEAT_TIMEOUT_MS + 200U)==SYS_OK);CHECK(s_test_runtime.status.timeout_events==2U);break;
      case 39:CHECK(IPC_Service_GetRuntimeStatus(NULL)==SYS_ERR_INVALID_ARG);break;
      case 40:CHECK(IPC_Service_GetRuntimeStatus((IpcRuntimeStatus_t *)0xFFFFFFF0UL)==SYS_ERR_INVALID_ARG);break;
      case 41:before=Digest(Peer(),sizeof(*Peer()));CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);CHECK(before==Digest(Peer(),sizeof(*Peer())));break;
      case 42:CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);CHECK(Local()->owner_id==LOCAL_OWNER);break;
      case 43:CHECK(sizeof(g_ipc)==320U && offsetof(IpcShared_t,m4)==192U);CHECK(Local()->reserved[4]==0U);break;
      case 44:s_thread=false;{IpcRuntimeStatus_t status;CHECK(IPC_Service_GetRuntimeStatus(&status)==SYS_ERR_INVALID_STATE);}break;
      case 45:Peer()->sequence=0U;CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTUP_TIMEOUT && s_test_runtime.status.busy_reads==1U);break;
      case 46:CHECK(IPC_Runtime_Process(&s_test_runtime,IPC_HEARTBEAT_TIMEOUT_MS)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_STARTUP_TIMEOUT);break;
      case 49:PeerHeartbeat(0xFFFFFF00UL);CHECK(IPC_Runtime_Process(&s_test_runtime,100U)==SYS_OK);CHECK(s_test_runtime.status.peer_state==IPC_PEER_ONLINE);break;
      default:return __LINE__;
    }
    return 0;
}
