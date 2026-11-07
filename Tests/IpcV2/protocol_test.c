#include "ipc_core.h"
#include "ipc_platform.h"
#include <stddef.h>

#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

//只在PC模拟测试使用，不向正式固件提供测试任务
static uint32_t s_mask;
static bool s_thread = true;
static bool s_bad_port;
static uint32_t s_hook_mode;
static uint32_t s_hook_word;
static bool s_hook_busy;
static IpcState_t s_new = {IPC_CORE_READY, 0x12345678U, 0x20U, SYS_ERR_IO};
volatile uint32_t g_hook_failure;
volatile uint32_t g_read_sequences;
volatile uint32_t g_barriers;
volatile int32_t g_hook_result;

//按字节填充普通或模拟共享RAM，避免测试夹具依赖标准库
static void Fill(void *memory, uint8_t value, size_t bytes)
{
    volatile uint8_t *p = memory;
    while (bytes-- != 0U) *p++ = value;
}

//检查失败时调用者输出保持原值
static bool Filled(const void *memory, uint8_t value, size_t bytes)
{
    const volatile uint8_t *p = memory;
    while (bytes-- != 0U) if (*p++ != value) return false;
    return true;
}

//用于核对另一核区域没有被协议接口修改
static uint32_t Digest(const volatile void *memory, size_t bytes)
{
    const volatile uint8_t *p = memory;
    uint32_t value = 2166136261UL;
    while (bytes-- != 0U) { value ^= *p++; value *= 16777619UL; }
    return value;
}

void __aeabi_memcpy(void *d, const void *s, size_t n)
{
    volatile uint8_t *p=d;const volatile uint8_t *q=s;
    while(n-- != 0U)*p++=*q++;
}
void __aeabi_memcpy4(void *d,const void *s,size_t n){__aeabi_memcpy(d,s,n);}
void __aeabi_memclr(void *d,size_t n){Fill(d,0U,n);}
void __aeabi_memclr4(void *d,size_t n){Fill(d,0U,n);}

//记录屏障调用，不能据此代替实板总线顺序验证
static void Barrier(void){g_barriers++;}
static uint32_t Enter(void){uint32_t previous=s_mask;s_mask=1U;return previous;}
static void Leave(uint32_t previous){s_mask=previous;}
static bool IsThread(void){return s_thread;}
static const IpcCorePort_t s_port={Barrier,Enter,Leave,IsThread};
const IpcCorePort_t *IPC_Platform_GetPort(void){return s_bad_port ? NULL : &s_port;}
void IPC_Platform_MemoryBarrier(void){Barrier();}

#if defined(CORE_CM7)
#include "../../CM7/Service/ipc_service.c"
#define LOCAL_OWNER IPC_OWNER_CM7
#define PEER_OWNER IPC_OWNER_CM4
static volatile IpcStatusBlock_t *Local(void){return &g_ipc.m7;}
static volatile IpcStatusBlock_t *Peer(void){return &g_ipc.m4;}
#else
#include "../../CM4/Service/ipc_service.c"
#define LOCAL_OWNER IPC_OWNER_CM4
#define PEER_OWNER IPC_OWNER_CM7
static volatile IpcStatusBlock_t *Local(void){return &g_ipc.m4;}
static volatile IpcStatusBlock_t *Peer(void){return &g_ipc.m7;}
#endif

//模拟另一核已经建立协议，仅是测试夹具的内存准备
static void SeedHeader(void)
{
    Fill((void *)&g_ipc.header,0U,sizeof(g_ipc.header));
    g_ipc.header.magic=IPC_MAGIC;g_ipc.header.version=IPC_VERSION;
    g_ipc.header.layout_bytes=IPC_LAYOUT_BYTES;g_ipc.header.header_bytes=IPC_HEADER_BYTES;
    g_ipc.header.status_block_bytes=IPC_STATUS_BYTES;g_ipc.header.feature_flags=IPC_FEATURE_SEQLOCK;
}

//模拟对端提交，不复制一份生产序号算法
static void SeedPeer(uint32_t sequence)
{
    Fill((void *)Peer(),0U,sizeof(*Peer()));
    Peer()->owner_id=PEER_OWNER;Peer()->block_bytes=IPC_STATUS_BYTES;
    Peer()->ready=IPC_CORE_READY;Peer()->heartbeat=17U;
    Peer()->fault_flags=IPC_FAULT_SHARED_INVALID;Peer()->last_result=SYS_ERR_PROTOCOL;
    Peer()->sequence=sequence;
}

static bool NewValue(const IpcStatusSnapshot_t *snapshot)
{
    return snapshot->ready==s_new.ready && snapshot->heartbeat==s_new.heartbeat &&
        snapshot->fault_flags==s_new.fault_flags && snapshot->last_result==s_new.last_result;
}

//仅在实际读写访问点触发，交错调用仍执行正式协议源码
void IPC_TestAfterAccess(uint32_t operation,uint32_t word)
{
    IpcStatusSnapshot_t snapshot;
    SysResult_t result;
    uint32_t mode=s_hook_mode;
    if(s_hook_busy)return;
    if(operation==3U && word==0U)g_read_sequences++;
    if(mode==0U)return;
    s_hook_busy=true;
    if(mode==1U && operation==1U && word==s_hook_word)
    {
        Fill(&snapshot,0xCDU,sizeof(snapshot));
        result=IPC_Core_ReadLocal(&snapshot);
        if(result!=SYS_ERR_BUSY || !Filled(&snapshot,0xCDU,sizeof(snapshot)))g_hook_failure=__LINE__;
    }
    else if(mode==2U && operation==3U && word==s_hook_word)
    {
        s_hook_mode=0U;
        g_hook_result=IPC_Core_PublishLocal(&s_new);
    }
    else if(mode==3U && operation==3U && word==0U)
        g_hook_result=IPC_Core_PublishLocal(&s_new);
    else if(mode==4U && operation==3U && word==4U)
    {
        s_hook_mode=0U;g_ipc.header.version=99U;
    }
    else if(mode==5U && operation==3U && word==0U)
    {
        s_hook_mode=0U;SeedPeer(4U);
    }
    else if(mode==6U && operation==1U && word==3U)
    {
        s_hook_mode=0U;
        if(IPC_Core_PublishLocal(&s_new)!=SYS_ERR_BUSY)g_hook_failure=__LINE__;
        if(IPC_Core_EndInitialization()!=SYS_ERR_BUSY)g_hook_failure=__LINE__;
    }
    else if(mode==8U && operation==4U)
    {
        s_hook_mode=0U;g_hook_result=IPC_Core_PublishLocal(&s_new);
    }
    else if(mode==7U && operation==2U)
    {
        s_hook_mode=0U;
        result=IPC_Core_ReadLocal(&snapshot);
        if(result!=SYS_OK || !NewValue(&snapshot))g_hook_failure=__LINE__;
    }
    s_hook_busy=false;
}

//建立本核初始状态，模拟器每例使用全新RAM与本地静态上下文
static int Setup(void)
{
#if defined(CORE_CM4)
    const IpcState_t initial={IPC_CORE_NOT_READY,0U,0U,SYS_OK};
#endif
    CHECK(IPC_Core_Bind(&g_ipc,&s_port)==SYS_OK);
#if defined(CORE_CM7)
    CHECK(IPC_Core_ColdStart()==SYS_OK);
#else
    SeedHeader();
    CHECK(IPC_Core_InitializeLocal(&initial)==SYS_OK);
#endif
    SeedPeer(2U);
    return 0;
}

//执行协议和Service初始化测试
int Ipc_TestRun(uint32_t id)
{
    IpcStatusSnapshot_t snapshot;
    IpcDiagnostics_t diagnostics;
    IpcState_t state={IPC_CORE_READY,9U,IPC_FAULT_HSEM,SYS_ERR_IO};
    uint32_t before,peer_before;
    IpcCorePort_t port=s_port;
    Fill((void *)&g_ipc,0xA5U,sizeof(g_ipc));

    if(id==0U)
    {
        CHECK(sizeof(g_ipc)==320U && sizeof(g_ipc.header)==64U && sizeof(g_ipc.m7)==128U);
        CHECK(offsetof(IpcShared_t,m7)==0x40U && offsetof(IpcShared_t,m4)==0xC0U);
        CHECK(((uintptr_t)&g_ipc & 31U)==0U);return 0;
    }
    if(id<=5U)
    {
        before=Digest(&g_ipc,sizeof(g_ipc));
        if(id==1U)CHECK(IPC_Core_Bind(NULL,&s_port)==SYS_ERR_INVALID_ARG);
        if(id==2U)CHECK(IPC_Core_Bind((volatile IpcShared_t *)((uintptr_t)&g_ipc+4U),&s_port)==SYS_ERR_INVALID_ARG);
        if(id==3U){port.barrier=NULL;CHECK(IPC_Core_Bind(&g_ipc,&port)==SYS_ERR_INVALID_ARG);}
        if(id==4U){s_thread=false;CHECK(IPC_Core_Bind(&g_ipc,&s_port)==SYS_ERR_INVALID_STATE);}
        if(id==5U){CHECK(IPC_Core_Bind(&g_ipc,&s_port)==SYS_OK);CHECK(IPC_Core_Bind(&g_ipc,&s_port)==SYS_ERR_ALREADY_EXISTS);}
        CHECK(Digest(&g_ipc,sizeof(g_ipc))==before);return 0;
    }
    if(id==63U)
    {
        before=Digest(&g_ipc,IPC_SHARED_WINDOW_BYTES);
        CHECK(IPC_Core_Bind(&g_ipc,(const IpcCorePort_t *)((uintptr_t)&g_ipc+512U))==SYS_ERR_INVALID_ARG);
        CHECK(before==Digest(&g_ipc,IPC_SHARED_WINDOW_BYTES));return 0;
    }
    if(id==32U)
    {
        before=Digest(&g_ipc,sizeof(g_ipc));
#if defined(CORE_CM7)
        CHECK(IPC_Core_ColdStart()==SYS_ERR_INVALID_STATE);
#else
        CHECK(IPC_Core_InitializeLocal(&state)==SYS_ERR_INVALID_STATE);
#endif
        CHECK(Digest(&g_ipc,sizeof(g_ipc))==before);return 0;
    }
    if(id>=44U && id<=47U)
    {
        if(id==47U)
        {
            CHECK(IPC_Service_ReadLocal(&snapshot)==SYS_ERR_INVALID_STATE);
            CHECK(IPC_Service_GetDiagnostics(&diagnostics)==SYS_OK);return 0;
        }
        if(id==45U)
        {
#if defined(CORE_CM7)
            before=Digest(&g_ipc,sizeof(g_ipc));
            CHECK(IPC_Service_Init()==SYS_ERR_INVALID_STATE);
#else
            SeedHeader();g_ipc.header.version=1U;before=Digest(&g_ipc,sizeof(g_ipc));
            CHECK(IPC_Service_Init()==SYS_ERR_PROTOCOL);
#endif
            CHECK(Digest(&g_ipc,sizeof(g_ipc))==before);return 0;
        }
#if defined(CORE_CM7)
        CHECK(IPC_Service_EarlyInit()==SYS_OK);
#else
        SeedHeader();
#endif
        CHECK(IPC_Service_Init()==SYS_OK);
        CHECK(IPC_Service_ReadLocal(&snapshot)==SYS_OK);
        CHECK(snapshot.ready==IPC_CORE_READY && snapshot.heartbeat==0U);
        CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_INVALID_STATE);
        if(id==46U)CHECK(IPC_Service_Init()==SYS_ERR_ALREADY_EXISTS);
        return 0;
    }
    if(id==8U || id==56U || id==57U)
    {
        CHECK(IPC_Core_Bind(&g_ipc,&s_port)==SYS_OK);
        if(id==8U)
        {
            peer_before=Digest(Peer(),sizeof(*Peer()));
#if defined(CORE_CM7)
            CHECK(IPC_Core_ColdStart()==SYS_OK);
#else
            SeedHeader();before=Digest(&g_ipc.header,sizeof(g_ipc.header));
            CHECK(IPC_Core_InitializeLocal(&state)==SYS_OK);
            CHECK(before==Digest(&g_ipc.header,sizeof(g_ipc.header)));
#endif
            CHECK(peer_before==Digest(Peer(),sizeof(*Peer())));
        }
        else
        {
            SeedHeader();
            if(id==56U)g_ipc.header.version=1U;
            else state.ready=2U;
            before=Digest(&g_ipc,sizeof(g_ipc));
            CHECK(IPC_Core_InitializeLocal(&state)==(id==56U?SYS_ERR_PROTOCOL:SYS_ERR_INVALID_ARG));
            CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));
        }
        return 0;
    }
    if(id==58U || id==59U)
    {
#if defined(CORE_CM4)
        SeedHeader();
#endif
        before=Digest(&g_ipc,sizeof(g_ipc));
        if(id==58U)s_thread=false;else s_bad_port=true;
#if defined(CORE_CM7)
        CHECK(IPC_Service_EarlyInit()==(id==58U?SYS_ERR_INVALID_STATE:SYS_ERR_INVALID_ARG));
#else
        CHECK(IPC_Service_Init()==(id==58U?SYS_ERR_INVALID_STATE:SYS_ERR_INVALID_ARG));
#endif
        CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));return 0;
    }
    CHECK(Setup()==0);
    Fill(&snapshot,0xCDU,sizeof(snapshot));
    peer_before=Digest(Peer(),sizeof(*Peer()));

    if(id>=100U && id<132U)
    {
        s_hook_mode=1U;s_hook_word=id-100U;
        CHECK(IPC_Core_PublishLocal(&s_new)==SYS_OK);s_hook_mode=0U;
        CHECK(g_hook_failure==0U);CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);
        CHECK(NewValue(&snapshot));CHECK(Digest(Peer(),sizeof(*Peer()))==peer_before);return 0;
    }
    if(id>=200U && id<232U)
    {
        s_hook_mode=2U;s_hook_word=id-200U;
        CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);
        CHECK(g_hook_result==SYS_OK && NewValue(&snapshot));
        CHECK(g_read_sequences==2U);CHECK(Digest(Peer(),sizeof(*Peer()))==peer_before);return 0;
    }
    switch(id)
    {
      case 6:CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(snapshot.sequence==2U && snapshot.owner_id==LOCAL_OWNER);break;
      case 7:CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_OK);CHECK(snapshot.owner_id==PEER_OWNER && snapshot.heartbeat==17U);break;
      case 8:CHECK(Digest(Peer(),sizeof(*Peer()))==peer_before);CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(snapshot.ready==0U);break;
      case 9:g_ipc.header.magic=0U;break;
      case 10:g_ipc.header.version=1U;break;
      case 11:g_ipc.header.layout_bytes=319U;break;
      case 12:g_ipc.header.header_bytes=32U;break;
      case 13:g_ipc.header.status_block_bytes=124U;break;
      case 14:g_ipc.header.feature_flags=0x80000000U;break;
      case 15:g_ipc.header.reserved[9]=1U;break;
      case 16:CHECK(IPC_Core_ReadLocal(NULL)==SYS_ERR_INVALID_ARG);break;
      case 17:before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_ReadPeer((IpcStatusSnapshot_t *)&g_ipc.m7)==SYS_ERR_INVALID_ARG);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));break;
      case 18:CHECK(IPC_Core_PublishLocal(NULL)==SYS_ERR_INVALID_ARG);break;
      case 19:before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_PublishLocal((const IpcState_t *)Peer())==SYS_ERR_INVALID_ARG);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));break;
      case 20:state.ready=2U;CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_INVALID_ARG);break;
      case 21:state.last_result=1;CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_INVALID_ARG);break;
      case 22:CHECK(IPC_Core_PublishLocal(&state)==SYS_OK);CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(snapshot.sequence==4U && snapshot.heartbeat==9U && snapshot.last_result==SYS_ERR_IO);break;
      case 23:Local()->sequence=3U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_BUSY);CHECK(g_read_sequences==3U && Filled(&snapshot,0xCDU,sizeof(snapshot)));CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_BUSY);break;
      case 24:Local()->sequence=0U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_BUSY);CHECK(g_read_sequences==3U);CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_INVALID_STATE);break;
      case 25:Local()->owner_id=99U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_PROTOCOL);break;
      case 26:Local()->block_bytes=124U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_PROTOCOL);break;
      case 27:Local()->reserved[24]=1U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_PROTOCOL);break;
      case 28:Local()->ready=2U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_PROTOCOL);Local()->ready=1U;Local()->last_result=1;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_PROTOCOL);break;
      case 29:Local()->sequence=0xFFFFFFFCUL;CHECK(IPC_Core_PublishLocal(&state)==SYS_OK);CHECK(Local()->sequence==0xFFFFFFFEUL);before=Digest(Local(),sizeof(*Local()));CHECK(IPC_Core_PublishLocal(&s_new)==SYS_ERR_NO_RESOURCE);CHECK(before==Digest(Local(),sizeof(*Local())));break;
      case 30:Local()->sequence=0xFFFFFFFCUL;s_hook_mode=2U;s_hook_word=0U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(snapshot.sequence==0xFFFFFFFEUL && NewValue(&snapshot));break;
      case 31:g_ipc.header.version=99U;before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_PROTOCOL);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));break;
      case 33:
#if defined(CORE_CM7)
        CHECK(IPC_Core_ColdStart()==SYS_ERR_ALREADY_EXISTS);
#else
        CHECK(IPC_Core_InitializeLocal(&state)==SYS_ERR_ALREADY_EXISTS);
#endif
        break;
      case 34:CHECK(IPC_Core_EndInitialization()==SYS_OK);before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_INVALID_STATE);CHECK(IPC_Core_InitializeLocal(&state)==SYS_ERR_INVALID_STATE);CHECK(IPC_Core_EndInitialization()==SYS_ERR_ALREADY_EXISTS);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));break;
      case 35:s_hook_mode=6U;CHECK(IPC_Core_PublishLocal(&s_new)==SYS_OK);CHECK(g_hook_failure==0U);break;
      case 36:s_hook_mode=7U;CHECK(IPC_Core_PublishLocal(&s_new)==SYS_OK);CHECK(g_hook_failure==0U);break;
      case 37:s_hook_mode=2U;s_hook_word=4U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(NewValue(&snapshot));break;
      case 38:s_hook_mode=3U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_BUSY);s_hook_mode=0U;CHECK(g_read_sequences==3U && Filled(&snapshot,0xCDU,sizeof(snapshot)));break;
      case 39:s_hook_mode=4U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_PROTOCOL);CHECK(Filled(&snapshot,0xCDU,sizeof(snapshot)));break;
      case 40:CHECK(IPC_Core_PublishLocal((const IpcState_t *)((uintptr_t)&state+1U))==SYS_ERR_INVALID_ARG);break;
      case 41:CHECK(IPC_Core_ReadLocal((IpcStatusSnapshot_t *)((uintptr_t)&snapshot+1U))==SYS_ERR_INVALID_ARG);break;
      case 42:before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_GetDiagnostics((IpcDiagnostics_t *)&g_ipc)==SYS_ERR_INVALID_ARG);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));break;
      case 43:s_mask=1U;CHECK(IPC_Core_PublishLocal(&state)==SYS_OK);CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(s_mask==1U);s_mask=0U;break;
      case 48:CHECK(IPC_Core_ReadLocal((IpcStatusSnapshot_t *)0xFFFFFFE0UL)==SYS_ERR_INVALID_ARG);CHECK(IPC_Core_PublishLocal((const IpcState_t *)0xFFFFFFF8UL)==SYS_ERR_INVALID_ARG);break;
      case 49:g_ipc.header.version=1U;before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_PROTOCOL);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));break;
      case 50:SeedPeer(3U);s_hook_mode=5U;CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_OK);CHECK(g_read_sequences==2U && snapshot.sequence==4U);break;
      case 51:Peer()->sequence=0xFFFFFFFFUL;CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_BUSY);CHECK(g_read_sequences==3U);break;
      case 52:CHECK(IPC_Core_PublishLocal(&s_new)==SYS_OK);CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(NewValue(&snapshot) && g_barriers>0U);break;
      case 53:s_thread=false;before=Digest(&g_ipc,sizeof(g_ipc));CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_ERR_INVALID_STATE);CHECK(IPC_Core_PublishLocal(&state)==SYS_ERR_INVALID_STATE);CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));s_thread=true;break;
      case 54:CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(snapshot.heartbeat==0U);CHECK(IPC_Core_EndInitialization()==SYS_OK);CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(snapshot.heartbeat==0U);break;
      case 55:Peer()->owner_id=LOCAL_OWNER;CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_PROTOCOL);break;
      case 60:s_hook_mode=8U;CHECK(IPC_Core_ReadLocal(&snapshot)==SYS_OK);CHECK(snapshot.sequence==2U && snapshot.ready==0U && snapshot.heartbeat==0U);CHECK(Local()->sequence==4U && g_hook_result==SYS_OK);break;
      case 61:before=Digest(&g_ipc,IPC_SHARED_WINDOW_BYTES);CHECK(IPC_Core_ReadLocal((IpcStatusSnapshot_t *)((uintptr_t)&g_ipc+512U))==SYS_ERR_INVALID_ARG);CHECK(before==Digest(&g_ipc,IPC_SHARED_WINDOW_BYTES));break;
      case 62:before=Digest(&g_ipc,IPC_SHARED_WINDOW_BYTES);CHECK(IPC_Core_PublishLocal((const IpcState_t *)((uintptr_t)&g_ipc+512U))==SYS_ERR_INVALID_ARG);CHECK(before==Digest(&g_ipc,IPC_SHARED_WINDOW_BYTES));break;
      default:return __LINE__;
    }
    if(id>=9U && id<=15U)
    {
        before=Digest(&g_ipc,sizeof(g_ipc));
        CHECK(IPC_Core_ReadPeer(&snapshot)==SYS_ERR_PROTOCOL);
        CHECK(g_read_sequences==0U);
        CHECK(Filled(&snapshot,0xCDU,sizeof(snapshot)));
        CHECK(before==Digest(&g_ipc,sizeof(g_ipc)));
        CHECK(IPC_Core_GetDiagnostics(&diagnostics)==SYS_OK);
        CHECK(diagnostics.fault_flags!=0U);
    }
    if(id==25U || id==26U || id==27U || id==28U || id==55U)
        CHECK(Filled(&snapshot,0xCDU,sizeof(snapshot)));
    if(id!=50U && id!=51U && id!=55U)CHECK(Digest(Peer(),sizeof(*Peer()))==peer_before);
    CHECK(g_hook_failure==0U && s_mask==0U);
    return 0;
}

#if defined(CORE_CM4)
//新增CM4 Timer依赖的测试桩，原05B测试断言保持不变
FrameworkTimerConfig_t g_test_timer_config;
static uint32_t g_test_tick;
static uint32_t g_test_timer_creates;
static uint32_t g_test_timer_starts;
static uint32_t g_test_timer_deletes;
static SysResult_t g_test_create_result;
static SysResult_t g_test_start_result;
uint32_t Platform_TimeGetMs(void){return g_test_tick;}
SysResult_t Framework_TimerCreate(const FrameworkTimerConfig_t *config, FrameworkTimerHandle_t *handle)
{
    g_test_timer_creates++;
    if(g_test_create_result!=SYS_OK)return g_test_create_result;
    g_test_timer_config=*config;*handle=1U;return SYS_OK;
}
SysResult_t Framework_TimerStart(FrameworkTimerHandle_t handle)
{
    (void)handle;g_test_timer_starts++;return g_test_start_result;
}
SysResult_t Framework_TimerDelete(FrameworkTimerHandle_t handle)
{
    (void)handle;g_test_timer_deletes++;return SYS_OK;
}
#endif
