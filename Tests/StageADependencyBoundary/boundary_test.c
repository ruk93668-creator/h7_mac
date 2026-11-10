//直接编译正式同步接口和Media代码，不复制正式实现
#include "../../CM7/Platform/platform_sync.c"
#include "../../CM7/Media/Src/camera_capture.c"
#include <string.h>

//模拟CPU与BSP状态，验证转发不改变原始执行环境
static uint32_t mask, basepri, ipsr, dmbs, pending, delivered;
static uint32_t notifications, last_events, seen_mask, seen_ipsr, bind_calls;
static void *seen_context;
static BspDcmiEventCallback_t bound;
static void *bound_context;
static SysResult_t bind_result;
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
uint32_t Test_GetPrimask(void) { return mask; }
uint32_t Test_GetBasepri(void) { return basepri; }
uint32_t Test_GetIpsr(void) { return ipsr; }
uint32_t Test_GetPriority(IRQn_Type irq) { return (uint32_t)irq + 5U; }
void Test_Disable(void) { mask = 1U; }
void Test_Restore(uint32_t value) { mask=value; if (!mask && pending) { delivered++;pending=0U; } }
void Test_Dmb(void) { dmbs++; }
SysResult_t BSP_DCMI_SetEventCallback(BspDcmiEventCallback_t cb, void *ctx)
{
    bind_calls++;
    if (bind_result != SYS_OK) return bind_result;
    bound=cb;bound_context=ctx;return SYS_OK;
}
SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t target, uint32_t *buffer)
{ (void)target;(void)buffer;return SYS_OK; }
uint32_t Platform_TimeGetMs(void) { return 0U; }
static void Notify(uint32_t events, void *context)
{
    notifications++;last_events=events;seen_context=context;
    seen_mask=mask;seen_ipsr=ipsr;
}
int Boundary_Test(uint32_t id)
{
    uint32_t outer,inner,initial=id&1U;
    CameraConfig_t config={160U,120U,CAMERA_PIXEL_FORMAT_YUV422};
    if (id < 2U)
    {
        mask=initial;basepri=0x50U;ipsr=0U;
        outer=Platform_SyncGetPrimask();Platform_SyncDisableIrq();
        inner=Platform_SyncGetPrimask();Platform_SyncDisableIrq();
        pending=1U;
        Platform_SyncMemoryBarrier();Platform_SyncRestoreIrq(inner);
        CHECK(mask==1U && delivered==0U && dmbs==1U);
        Platform_SyncRestoreIrq(outer);
        CHECK(mask==initial && delivered==(initial?0U:1U));
        CHECK(Platform_SyncGetBasepri()==0x50U);
        if (initial) { Platform_SyncRestoreIrq(0U);CHECK(delivered==1U); }
        return 0;
    }
    if (id==2U)
    {
        ipsr=42U;basepri=0x60U;mask=1U;
        CHECK(Platform_SyncGetException()==42U && Platform_SyncGetPrimask()==1U);
        CHECK(Platform_SyncGetBasepri()==0x60U && Platform_SyncGetIrqPriority(9U)==14U);
        CHECK(dmbs==0U && mask==1U);return 0;
    }
    if (id==3U)
    {
        CHECK(CameraCapture_SetEventCallback(NULL,NULL)==SYS_ERR_INVALID_ARG);
        CHECK(bind_calls==0U);return 0;
    }
    if (id==4U)
    {
        s_capture.initialized=true;
        CHECK(CameraCapture_SetEventCallback(Notify,NULL)==SYS_ERR_INVALID_STATE);
        CHECK(bind_calls==0U);return 0;
    }
    if (id==5U)
    {
        bind_result=SYS_ERR_BUSY;
        CHECK(CameraCapture_SetEventCallback(Notify,NULL)==SYS_ERR_BUSY);
        CHECK(s_capture_callback==Notify && bound==NULL);
        bind_result=SYS_OK;
        CHECK(CameraCapture_SetEventCallback(Notify,(void*)0x1234U)==SYS_ERR_INVALID_STATE);
        CHECK(bind_calls==1U && bound==NULL);return 0;
    }
    CHECK(CameraCapture_SetEventCallback(Notify,(void*)0x1234U)==SYS_OK);
    CHECK(CameraCapture_SetEventCallback(Notify,NULL)==SYS_ERR_INVALID_STATE);
    CHECK(bind_calls==1U && bound!=NULL);
    if (id==6U)
    {
        CHECK(CameraStream_Init(&s_capture.stream,&config)==SYS_OK);
        CHECK(CameraStream_OnFrameComplete(BSP_DCMI_TARGET_M0,&s_capture.stream)==SYS_OK);
        CHECK(notifications==1U && last_events==CAMERA_CAPTURE_EVENT_PROGRESS);
        CHECK(seen_context==(void*)0x1234U);return 0;
    }
    //同一回调分别覆盖任务、IRQ和原本已屏蔽的调用环境
    mask=(id>=12U);ipsr=(id>=9U?37U:0U);
    if (id%3U==1U) { bound(BSP_DCMI_NOTIFY_PROGRESS,bound_context);CHECK(last_events==CAMERA_CAPTURE_EVENT_PROGRESS); }
    if (id%3U==2U) { bound(BSP_DCMI_NOTIFY_ERROR,bound_context);CHECK(last_events==CAMERA_CAPTURE_EVENT_ERROR); }
    if (id%3U==0U) { bound(BSP_DCMI_NOTIFY_PROGRESS|BSP_DCMI_NOTIFY_ERROR,bound_context);CHECK(last_events==(CAMERA_CAPTURE_EVENT_PROGRESS|CAMERA_CAPTURE_EVENT_ERROR)); }
    CHECK(notifications==1U && seen_mask==mask && seen_ipsr==ipsr);
    CHECK(seen_context==(void*)0x1234U && dmbs==0U);return 0;
}

//未执行的硬件路径明确拒绝，避免测试桩伪造采集成功
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *n) {(void)n;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_StartSnapshot(uint32_t *p,uint32_t n) {(void)p;(void)n;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *p,uint32_t n) {(void)p;(void)n;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_StartContinuous(uint32_t *a,uint32_t *b,uint32_t n,BspDcmiFrameCallback_t cb,void *c)
{(void)a;(void)b;(void)n;(void)cb;(void)c;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t BSP_DCMI_Stop(void) {return SYS_ERR_NOT_SUPPORTED;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *e) {(void)e;return false;}
SysResult_t CameraMjpegFifo_Start(const CameraConfig_t *c) {(void)c;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t CameraMjpegFifo_Process(void) {return SYS_ERR_NOT_SUPPORTED;}
bool CameraMjpegFifo_GetFrame(CameraFrame_t *f) {(void)f;return false;}
void CameraMjpegFifo_GetStats(uint32_t *a,uint32_t *b) {*a=0U;*b=0U;}
bool CameraMjpegFifo_HasLoan(void) {return false;}
void CameraMjpegFifo_OnStopped(void) {}
SysResult_t CameraMjpegFifo_ReleaseFrame(void) {return SYS_ERR_NOT_SUPPORTED;}
