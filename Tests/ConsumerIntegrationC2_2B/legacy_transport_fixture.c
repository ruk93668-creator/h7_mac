#include "app_camera.h"
#include "video_config.h"

//旧停止专项不接生命周期总线，只补齐新增链接依赖，不提供归还证明
//若专项链接真实资格实现，强符号覆盖以下运输缺席桩
__weak bool App_LifecycleMessagesReceive(AppLifecycleChannel_t channel, AppLifecycleMessage_t *message)
{ (void)channel; (void)message; return false; }
__weak SysResult_t App_LifecycleMessagesSend(AppLifecycleChannel_t channel, const AppLifecycleMessage_t *message)
{ (void)channel; (void)message; return SYS_ERR_NO_RESOURCE; }
__weak SysResult_t App_CameraQualificationPrepare(void) { return SYS_ERR_NOT_SUPPORTED; }
__weak SysResult_t App_CameraQualificationReceive(const AppLifecycleMessage_t *r, uint32_t *d)
{ (void)r; (void)d; return SYS_ERR_NOT_SUPPORTED; }
__weak SysResult_t App_CameraQualificationProcess(void) { return SYS_ERR_NOT_SUPPORTED; }
__weak SysResult_t App_CameraQualificationFlush(uint32_t c, AppLifecycleSendFn s, void *x)
{ (void)c; (void)s; (void)x; return SYS_ERR_NOT_SUPPORTED; }
__weak SysResult_t App_CameraQualificationAccess(AppCameraQualification_t **c, const AppQualificationResourceOps_t **o, bool p)
{ (void)c; (void)o; (void)p; return SYS_ERR_INVALID_STATE; }
__weak bool App_MessagesIsOwner(RuntimeEventTarget_t t) { (void)t; return false; }
__weak SysResult_t App_TasksReleaseBoot(void) { return SYS_OK; }

//仅旧业务夹具缺少资格工具时转发原编号分配原语；真实核心强符号优先
__weak SysResult_t App_QualificationNextGeneration(uint32_t *counter,uint32_t *generation)
{ return App_LifecycleAllocateIds(counter,1U,generation); }
//旧USB夹具没有产品配置，默认提供原UVC产品；真实配置强符号优先
__weak const VideoConfig_t *VideoConfig_GetDefault(void)
{ static const VideoConfig_t value={{160,120,CAMERA_PIXEL_FORMAT_YUV422},VIDEO_OUTPUT_USB_UVC,25,0};return &value; }

//旧任务夹具不运行调度器，自唤醒仅作为调度提示
__weak SysResult_t RuntimeEvent_Post(RuntimeEventTarget_t t,uint32_t e){(void)t;(void)e;return SYS_OK;}
