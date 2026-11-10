//执行真实CameraTask、App_Camera和Media停止路径，BSP与JPEG FIFO使用边界桩
#define B1_REAL_MEDIA
#include "behavior_test.c"
#include "../../CM7/Media/Src/camera_capture.c"

//保存BSP完成回调和可控错误，禁止访问真实DCMI
static BspDcmiFrameCallback_t complete;
static void *complete_context;
static bool jpeg_loan, hw_stopped;
static uint32_t injected_error;
#define CHECK(x) do{if(!(x))return __LINE__;}while(0)

//复用真实Media状态机，BSP仅模拟成功和失败结果
SysResult_t BSP_DCMI_SetEventCallback(BspDcmiEventCallback_t c,void*p){(void)c;(void)p;return SYS_OK;}
SysResult_t BSP_DCMI_StartSnapshot(uint32_t*p,uint32_t n){(void)p;(void)n;return SYS_OK;}
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t*p,uint32_t n){(void)p;(void)n;return SYS_OK;}
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t*n){(void)n;return SYS_ERR_BUSY;}
SysResult_t BSP_DCMI_StartContinuous(uint32_t*a,uint32_t*b,uint32_t n,BspDcmiFrameCallback_t c,void*p)
{(void)a;(void)b;(void)n;complete=c;complete_context=p;return start_result;}
SysResult_t BSP_DCMI_ReplaceTarget(BspDcmiTarget_t t,uint32_t*p){(void)t;(void)p;return SYS_OK;}
SysResult_t BSP_DCMI_Stop(void){if(stop_busy_rounds){stop_busy_rounds--;return SYS_ERR_BUSY;}if(stop_error_rounds){stop_error_rounds--;return SYS_ERR_IO;}hw_stopped=true;return SYS_OK;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t*e){if(!injected_error)return false;memset(e,0,sizeof(*e));e->flags=BSP_DCMI_EVENT_ERROR;e->result=SYS_ERR_IO;injected_error=0;return true;}

//FIFO桩只提供借用和归还条件，不替代Media的DRAINING判断
SysResult_t CameraMjpegFifo_Start(const CameraConfig_t*c){(void)c;return SYS_OK;}
SysResult_t CameraMjpegFifo_Process(void){return SYS_OK;}
bool CameraMjpegFifo_HasLoan(void){return jpeg_loan;}
void CameraMjpegFifo_OnStopped(void){}
void CameraMjpegFifo_GetStats(uint32_t*a,uint32_t*b){*a=10;*b=2;}
bool CameraMjpegFifo_GetFrame(CameraFrame_t*f){(void)f;return false;}
SysResult_t CameraMjpegFifo_ReleaseFrame(void){jpeg_loan=false;return SYS_OK;}

//覆盖真实Media的持帧、硬件错误、DRAINING及传感器停止依赖
int B1_MediaRun(uint32_t id)
{
    CameraFrame_t f;
    uint32_t i;
    sensor.base.state=DEVICE_STATE_READY;
    if(id>=2)config.camera.format=CAMERA_PIXEL_FORMAT_JPEG;
    App_CameraSetDiagnosticObserver(CameraTask_Observe);
    Push(APP_MESSAGE_START,1);Round(RUNTIME_EVENT_BOOT_READY);
    CHECK(s_camera.running&&s_capture.state==CAMERA_CAPTURE_CAPTURING);
    if(id==0){CHECK(complete!=NULL);CHECK(complete(BSP_DCMI_TARGET_M0,complete_context)==SYS_OK);CHECK(CameraCapture_StreamGetFrame(&f));Push(APP_MESSAGE_STOP,2);Round(0);CHECK(s_camera.stopping&&!hw_stopped);CHECK(CameraCapture_StreamReleaseFrame()==SYS_OK);Round(0);}
    if(id==1){stop_error_rounds=2;Push(APP_MESSAGE_STOP,2);Round(0);CHECK(s_camera.stopping&&s_camera.last_result==SYS_ERR_IO);for(i=0;i<3;i++)Round(0);}
    if(id==2){jpeg_loan=true;Push(APP_MESSAGE_STOP,2);Round(0);CHECK(s_camera.stopping&&!hw_stopped);jpeg_loan=false;Round(0);}
    if(id==3){jpeg_loan=true;injected_error=1;Round(0);CHECK(hw_stopped);CHECK(s_capture.state==CAMERA_CAPTURE_DRAINING);CHECK(!CameraCapture_IsStopped());CHECK(sensor.base.state==DEVICE_STATE_RUNNING);CHECK(s_camera.stopping);CHECK(CameraCapture_StreamReleaseFrame()==SYS_OK);CHECK(CameraCapture_IsStopped());Round(0);}
    if(id==4){injected_error=1;Round(0);CHECK(hw_stopped&&s_camera.fault==SYS_ERR_IO);}
    if(id==5){stop_busy_rounds=2;Push(APP_MESSAGE_STOP,2);for(i=0;i<4;i++)Round(0);}
    CHECK(hw_stopped&&CameraCapture_IsStopped());
    CHECK(!s_camera.running&&!s_camera.stopping);
    CHECK(sensor.base.state==DEVICE_STATE_READY);
    return 0;
}
