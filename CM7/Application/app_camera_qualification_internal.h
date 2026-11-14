#ifndef CM7_APP_CAMERA_QUALIFICATION_INTERNAL_H
#define CM7_APP_CAMERA_QUALIFICATION_INTERNAL_H
#include "app_camera_qualification.h"

//仅供App_Camera与资格核心内部适配，不是跨任务或业务可写访问接口
//调用者必须先通过资格适配中的任务/ISR校验，再短暂借用同一私有对象
SysResult_t App_CameraQualificationAccess(AppCameraQualification_t **core,
    const AppQualificationResourceOps_t **ops, bool preparing);
#endif
