#include "HardwareControl.h"
#include "control_task.h"
#include "app_storage.h"
#include "app_messages.h"
#include "platform_sync.h"
#include <stdbool.h>

#if !defined(APP_R2_HW_ACCEPTANCE_ENABLED) || APP_R2_HW_ACCEPTANCE_ENABLED != 1
#error HardwareControl_requires_independent_acceptance_build
#endif

//保存唯一Watch请求邮箱，调试器不直接操作业务模块私有状态
volatile R2HardwareMailbox_t g_r2_hw;

//保存已消费序号，禁止通过修改诊断回显重复执行请求
static uint32_t s_processed_sequence;

//阻止公共请求回调期间重入验收适配器
static bool s_processing;

//将冻结的操作码投递到现有公共请求入口
static SysResult_t R2_Dispatch(uint32_t command);

//投递现有业务请求，不直接执行文件、帧释放或硬件停止
static SysResult_t R2_Dispatch(uint32_t command)
{
    switch (command)
    {
        case R2_HW_SYSTEM_START: return ControlTask_RequestStart();
        case R2_HW_SYSTEM_STOP: return ControlTask_RequestStop();
        case R2_HW_STORAGE_VIDEO: return App_StorageStartRecording();
        case R2_HW_STORAGE_SNAPSHOT: return App_StorageRequestSnapshot();
        case R2_HW_STORAGE_STOP: return App_StorageStop();
        case R2_HW_STORAGE_RECOVER: return App_StorageRecover();
        default: return SYS_ERR_INVALID_ARG;
    }
}

//处理一笔已经提交的Watch请求
SysResult_t R2_HardwareControlProcess(void)
{
    uint32_t sequence, command;
    SysResult_t result;

    //只借用现有Control所有者，ISR和中断屏蔽期间不能提交请求
    if (Platform_SyncGetException() != 0U || Platform_SyncGetPrimask() != 0U ||
        Platform_SyncGetBasepri() != 0U || !App_MessagesIsOwner(RUNTIME_TARGET_CONTROL))
        return SYS_ERR_INVALID_STATE;
    if (s_processing)
        return SYS_ERR_BUSY;
    g_r2_hw.ready = 1U;

    //先读提交号再冻结操作码，未提交、重复和旧序号都不重复投递
    sequence = g_r2_hw.submit_seq;
    if (sequence == 0U || sequence <= s_processed_sequence)
        return SYS_OK;
    Platform_SyncMemoryBarrier();
    command = g_r2_hw.command;
    Platform_SyncMemoryBarrier();
    if (sequence != g_r2_hw.submit_seq || command != g_r2_hw.command)
        return SYS_ERR_BUSY;

    //先绑定序号再调用公共接口，失败也只记录一次，不自动重发
    s_processing = true;
    result = sequence == s_processed_sequence + 1U ? SYS_OK : SYS_ERR_PROTOCOL;
    s_processed_sequence = sequence;
    if (result == SYS_OK)
        result = R2_Dispatch(command);

    //按冻结请求发布结果，最后更新完成序号作为Watch读取边界
    g_r2_hw.processed_command = command;
    g_r2_hw.dispatch_result = result;
    Platform_SyncMemoryBarrier();
    g_r2_hw.processed_seq = sequence;
    s_processing = false;
    return result;
}
