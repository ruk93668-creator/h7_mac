#ifndef CM7_V3_SUPERVISOR_H
#define CM7_V3_SUPERVISOR_H

#include "../Runtime/service_events.h"

//监督器中Camera采集组件的活动位
#define APP_V3_PIPE_CAMERA  1U
//监督器中USB视频组件的活动位
#define APP_V3_PIPE_USB     2U
//监督器中录像组件的活动位
#define APP_V3_PIPE_RECORD  4U
//HTTP 摄像头消费者依赖 Camera，启停顺序由原监督器统一管理。
#define APP_V3_PIPE_NETWORK 8U

//由Control发布的组件状态与当前业务进展
typedef struct
{
    //已确认处于活动状态的组件集合
    uint32_t active;
    //还缺少终结证据的组件集合，保留对其依赖
    uint32_t uncertain;
    //当前步骤等待的命令号
    uint32_t pending_id;
    //当前步骤的目标组件位
    uint32_t pending_component;
    //本事务首个错误，后续清理结果不得覆盖
    SysResult_t first_fault;
    //有多组件依赖计划正在推进
    bool planning;
    //标记真实结果等待已超时，仍需处理迟到结果
    bool timed_out;
    //录像使用的采集配置或监督器观察到的Camera结果
    AppV3ServiceEvent_t camera;
    //监督器最后收到的USB业务终结
    AppV3ServiceEvent_t usb;
    //最近收到的Storage终结结果及真实卷状态
    AppV3ServiceEvent_t storage;
    //最近收到的 Network 真实生命周期终结。
    AppV3ServiceEvent_t network;
} AppV3SupervisorStatus_t;

//仅ControlTask调用。Set返回计划已受理；实际启停结果通过Read取得。
SysResult_t AppV3Supervisor_Set(uint32_t components, uint32_t file_number);
//由Control请求现有的显式恢复契约
SysResult_t AppV3Supervisor_Recover(uint32_t component);
//由Control请求刷新指定组件的真实状态
SysResult_t AppV3Supervisor_Query(uint32_t component);
//复制监督器当前只读状态，供协调调用者查询
SysResult_t AppV3Supervisor_Read(AppV3SupervisorStatus_t *status);
//由Control按依赖顺序推进业务计划和等待结果
void AppV3Supervisor_Poll(void);

#endif
