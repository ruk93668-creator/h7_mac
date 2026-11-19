#ifndef CM7_APP_CAMERA_QUALIFICATION_INTERNAL_H
#define CM7_APP_CAMERA_QUALIFICATION_INTERNAL_H
#include "app_camera_qualification.h"

//仅供App_Camera与资格核心内部适配，不是跨任务或业务可写访问接口
//调用者必须先通过资格适配中的任务/ISR校验，再短暂借用同一私有对象
SysResult_t App_CameraQualificationAccess(AppCameraQualification_t **core,
    const AppQualificationResourceOps_t **ops, bool preparing);
//以下资格接口仅允许CameraTask串行调用，不替换旧START/STOP
//首次显式准备工具，拒绝接管旧业务已经启动的资源
SysResult_t App_CameraQualificationPrepare(void);
//只接收协议请求，重复和候选不触发业务
SysResult_t App_CameraQualificationReceive(const AppLifecycleMessage_t *request, uint32_t *disposition);
//显式推进资格，真实归还证明未绑定时不能成功释放
SysResult_t App_CameraQualificationProcess(void);
//沿原运输函数可靠发送已固定回复
SysResult_t App_CameraQualificationFlush(uint32_t consumer, AppLifecycleSendFn send, void *context);
//只读当前资格，不作为帧释放证据
SysResult_t App_CameraQualificationGetStatus(uint32_t consumer, AppQualificationStatus_t *status);

#endif
