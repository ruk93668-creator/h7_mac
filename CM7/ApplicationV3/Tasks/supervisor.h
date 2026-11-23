#ifndef CM7_V3_SUPERVISOR_H
#define CM7_V3_SUPERVISOR_H

#include "../Runtime/service_events.h"

#define APP_V3_PIPE_CAMERA  1U
#define APP_V3_PIPE_USB     2U
#define APP_V3_PIPE_RECORD  4U

typedef struct
{
    uint32_t active;
    uint32_t uncertain;
    uint32_t pending_id;
    uint32_t pending_component;
    SysResult_t first_fault;
    bool planning;
    bool timed_out;
    AppV3ServiceEvent_t camera;
    AppV3ServiceEvent_t usb;
    AppV3ServiceEvent_t storage;
} AppV3SupervisorStatus_t;

//仅ControlTask调用。Set返回计划已受理；实际启停结果通过Read取得。
SysResult_t AppV3Supervisor_Set(uint32_t components, uint32_t file_number);
SysResult_t AppV3Supervisor_Recover(uint32_t component);
SysResult_t AppV3Supervisor_Query(uint32_t component);
SysResult_t AppV3Supervisor_Read(AppV3SupervisorStatus_t *status);
void AppV3Supervisor_Poll(void);

#endif
