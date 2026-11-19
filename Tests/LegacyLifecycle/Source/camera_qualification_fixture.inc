// @fixture include
#include "app_camera_qualification_internal.h"
// @fixture fields
    AppCameraQualification_t qualifications;//保存唯一资格核心，尚不接入业务消息
    bool resource_preparing;//同步资源启动正在执行
    bool resource_stop_confirmed;//实际硬件停止已经确认
    AppLifecycleIdentity_t stop_identity;//资格停止结果对应的完整事务身份
    AppQualificationStopResult_t stop_result;//冻结真实停止结果，后台恢复不改写
    bool stop_result_valid;//结果只允许对应请求消费
// @fixture declaration

//精确比较停止请求身份，禁止跨会话读取或消费结果
static bool App_CameraStopIdentityMatches(const AppLifecycleIdentity_t *identity);
// @fixture stop_confirmed
            s_camera.resource_stop_confirmed = s_camera.prepared;
// @fixture stop_result
        //请求结果只冻结一次，后续后台恢复不能把原失败改成成功
        if (s_camera.stop_result_valid && s_camera.stop_result.complete == 0U && result != SYS_ERR_BUSY)
        {
            s_camera.stop_result.result = result;
            s_camera.stop_result.hardware_stopped = result == SYS_OK && s_camera.resource_stop_confirmed;
            s_camera.stop_result.complete = 1U;
        }
// @fixture start_begin
    //记录启动窗口，旧的硬件停止确认不能用于新周期
    s_camera.resource_preparing = true;
    s_camera.resource_stop_confirmed = false;

// @fixture start_end
    s_camera.resource_preparing = false;
// @fixture adapter

//读取原资源权威字段，不另存运行或停止状态副本
static uint32_t App_CameraResourceState(void *context)
{
    (void)context;
    if (s_camera.resource_preparing)
        return APP_RESOURCE_PREPARING;
    if (s_camera.stopping)
        return s_camera.fault == SYS_OK ? APP_RESOURCE_STOPPING : APP_RESOURCE_FAULT_DRAINING;
    if (s_camera.running)
        return APP_RESOURCE_RUNNING;
    if (s_camera.resource_stop_confirmed)
        return APP_RESOURCE_STOPPED_CONFIRMED;
    return s_camera.prepared || s_camera.fault != SYS_OK ?
        APP_RESOURCE_FAULT_DRAINING : APP_RESOURCE_UNSTARTED;
}

//复用现有Device和Media启动路径
static SysResult_t App_CameraQualificationStart(void *context)
{
    SysResult_t result;
    (void)context;
    result = App_CameraStartResource();
    if (result != SYS_OK && result != SYS_ERR_BUSY)
        s_camera.fault = result;
    return result;
}

//精确比较停止请求身份
static bool App_CameraStopIdentityMatches(const AppLifecycleIdentity_t *identity)
{
    return identity != NULL && s_camera.stop_result_valid &&
        s_camera.stop_identity.session_id == identity->session_id &&
        s_camera.stop_identity.request_id == identity->request_id &&
        s_camera.stop_identity.consumer_id == identity->consumer_id &&
        s_camera.stop_identity.consumer_generation == identity->consumer_generation;
}

//登记资格停止请求，不调用Media或Device
static SysResult_t App_CameraRequestStop(void *context, const AppLifecycleIdentity_t *identity)
{
    (void)context;
    if (identity == NULL || identity->session_id == 0U || identity->request_id == 0U ||
        identity->consumer_generation == 0U ||
        (identity->consumer_id != APP_CONSUMER_USB && identity->consumer_id != APP_CONSUMER_STORAGE))
        return SYS_ERR_INVALID_ARG;
    if (s_camera.stop_result_valid)
        return App_CameraStopIdentityMatches(identity) ? SYS_OK : SYS_ERR_BUSY;

    //只绑定当前事务，不覆盖尚未消费的旧结果
    s_camera.stop_identity = *identity;
    s_camera.stop_result.complete = 0U;
    s_camera.stop_result.result = SYS_ERR_BUSY;
    s_camera.stop_result.hardware_stopped = 0U;
    s_camera.stop_result_valid = true;
    s_camera.stopping = true;
    return SYS_OK;
}

//读取对应请求的停止结果，不根据全局状态猜测完成
static SysResult_t App_CameraReadStopResult(void *context, const AppLifecycleIdentity_t *identity,
    AppQualificationStopResult_t *result)
{
    (void)context;
    if (identity == NULL || result == NULL)
        return SYS_ERR_INVALID_ARG;
    if (!s_camera.stop_result_valid)
        return SYS_ERR_NOT_FOUND;
    if (!App_CameraStopIdentityMatches(identity))
        return SYS_ERR_PROTOCOL;
    *result = s_camera.stop_result;
    return SYS_OK;
}

//在资格决策冻结后消费相同请求的完成记录
static SysResult_t App_CameraConsumeStopResult(void *context, const AppLifecycleIdentity_t *identity)
{
    (void)context;
    if (!App_CameraStopIdentityMatches(identity) || s_camera.stop_result.complete != 1U)
        return SYS_ERR_PROTOCOL;
    s_camera.stop_result_valid = false;
    return SYS_OK;
}

//未绑定真实归还证明，空回调必须阻止资格释放成功
static const AppQualificationResourceOps_t s_qualification_ops =
{
    App_CameraResourceState, App_CameraQualificationStart,
    App_CameraRequestStop, App_CameraReadStopResult, App_CameraConsumeStopResult, NULL, NULL
};

//仅向已经完成任务身份校验的同模块资格适配借出上下文
SysResult_t App_CameraQualificationAccess(AppCameraQualification_t **core,
    const AppQualificationResourceOps_t **ops, bool preparing)
{
    if (core == NULL || ops == NULL)
        return SYS_ERR_INVALID_ARG;
    if (preparing && (s_camera.running || s_camera.stopping || s_camera.prepared ||
        s_camera.initialized || s_camera.configured))
        return SYS_ERR_INVALID_STATE;
    *core = &s_camera.qualifications;
    *ops = &s_qualification_ops;
    return SYS_OK;
}
