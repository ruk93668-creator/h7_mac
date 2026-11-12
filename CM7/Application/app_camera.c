#include "app_camera.h"
#include "app_camera_qualification_internal.h"
#include "video_config.h"
#include "app_uvc_bridge.h"
#include "platform_time.h"
#include "camera_capture.h"
#include "ov5640_device.h"
#include <stddef.h>
#include <stdbool.h>

//集中保存配置、设备和业务状态，运行期只由CameraTask推进
typedef struct
{
    CameraConfig_t config;//保存已经接收的采集配置
    bool config_set;//记录配置是否已经确定
    bool configured;//记录传感器配置是否完成
    bool initialized;//记录Device和Media准备是否完成
    Device_t *device;//保存当前传感器Device
    bool running;//记录采集是否已经成功启动
    bool prepared;//保留原Media可推进及可停止判断
    bool stopping;//记录后台停止是否需要继续重试
    AppMessage_t ack;//保存当前命令对应的回复
    bool ack_pending;//记录ACK是否等待成功入队
    bool stop_command;//记录STOP命令是否尚待完成
    bool fault_sent;//记录本轮FAULT是否已经入队
    SysResult_t fault;//保存业务故障
    SysResult_t last_result;//保存最近业务操作结果
    uint32_t command_id;//保存最近接收的命令编号
    uint32_t fps_tick;//保存FPS统计窗口起点
    uint32_t fps_frames;//保存窗口起始帧数
    uint32_t captured_frames;//保存Media完成帧计数
    uint32_t dropped_frames;//保存Media丢帧计数
    uint32_t measured_fps_milli;//保存按原公式计算的采集FPS
    CameraConfig_t applied;//保存最近实际运行时采样的配置
    bool applied_valid;//记录是否采样过实际运行配置
    AppCameraQualification_t qualifications;//保存唯一资格核心，尚不接入业务消息
    bool resource_preparing;//同步资源启动正在执行
    bool resource_stop_confirmed;//实际硬件停止已经确认
    AppLifecycleIdentity_t stop_identity;//资格停止结果对应的完整事务身份
    AppQualificationStopResult_t stop_result;//冻结真实停止结果，后台恢复不改写
    bool stop_result_valid;//结果只允许对应请求消费
    bool round_open;//标记CameraTask本轮记账边界
    bool round_stop_attempted;//本轮App停止或START回退已经推进
    SysResult_t round_stop_result;//保留本轮实际停止失败
    uint32_t round_media_sequence;//记录进入本轮前的Media实际停止序号
    AppCameraDiagnosticObserver_t observer;//保存只读诊断投影入口
} AppCameraContext_t;

//保存唯一Camera业务上下文，Task不再持有业务副本
static AppCameraContext_t s_camera;

//在原Debug更新位置通知Task，不执行消息或硬件操作
static void App_CameraObserve(AppCameraDiagnosticField_t field);

//公共初始化，只配置OV5640和Media，不启动采集
static SysResult_t App_CameraPrepare(void);

//先启动传感器，再沿用Media的采集入口
static SysResult_t App_CameraStart(bool stream);

//复用旧START资源操作，不复制硬件启停算法
static SysResult_t App_CameraStartResource(void);

//推进实际停止，仅供轮末普通停止和START失败保护性回退使用
static SysResult_t App_CameraAdvanceStop(void);

//精确比较停止请求身份，禁止跨会话读取或消费结果
static bool App_CameraStopIdentityMatches(const AppLifecycleIdentity_t *identity);

//校验并保存唯一采集配置，不启动传感器
SysResult_t App_CameraConfigure(const CameraConfig_t *config)
{
    SysResult_t result;

    if (s_camera.config_set || s_camera.configured || s_camera.initialized)
        return SYS_ERR_INVALID_STATE;

    result = Ov5640_ValidateConfig(config);
    if (result != SYS_OK)
        return result;

    //JPEG采用变长接收，尺寸由Device及对应Media入口校验
    result = CameraConfig_IsVariableLength(config->format) ?
        SYS_OK : CameraCapture_ValidateStreamConfig(config);
    if (result != SYS_OK)
        return result;

    s_camera.config = *config;
    s_camera.config_set = true;
    return SYS_OK;
}

//返回已经确定的配置或原默认配置
const CameraConfig_t *App_CameraGetConfig(void)
{
	return s_camera.config_set ? &s_camera.config : &VideoConfig_GetDefault()->camera;
}

//沿用公共准备流程启动单帧采集
SysResult_t App_CameraInit(void)
{
	return App_CameraStart(false);
}

//沿用公共准备流程启动连续采集
SysResult_t App_CameraInitStream(void)
{
	return App_CameraStart(true);
}

//等待Media安全停止及归还完成后停止传感器
SysResult_t App_CameraStop(void)
{
    if (!s_camera.initialized || s_camera.device == NULL)
        return SYS_ERR_INVALID_STATE;

    //普通调用只登记停止意图，不能把受理当成硬件停止完成
    s_camera.stopping = true;
    return SYS_ERR_BUSY;
}

//推进实际停止并保留同轮尝试结果
static SysResult_t App_CameraAdvanceStop(void)
{
    CameraCaptureStopFacts_t facts;
    SysResult_t result;

    //START回退和轮末共享本轮事实，失败不能被同轮再次调用覆盖
    if (s_camera.round_open && s_camera.round_stop_attempted)
        return s_camera.round_stop_result;
    s_camera.round_stop_attempted = true;
    result = CameraCapture_GetStopFacts(&facts);
    if (result != SYS_OK)
        goto done;

    //Media保护性停止已经失败时，不在同一轮重复下发硬件操作
    if (s_camera.round_open && facts.attempt_sequence != s_camera.round_media_sequence &&
        facts.hardware_result != SYS_OK)
    {
        result = facts.hardware_result;
        goto done;
    }
    if (!s_camera.initialized || s_camera.device == NULL)
    {
        result = SYS_ERR_INVALID_STATE;
        goto done;
    }

    //同轮保护已完全停止时不重复Stop；其他轮次保留原Media检查和排空路径
    if ((facts.flags & CAMERA_STOP_COMPLETE) == 0U ||
        !s_camera.round_open || facts.attempt_sequence == s_camera.round_media_sequence)
    {
        result = CameraCapture_Stop();
        if (result != SYS_OK)
            goto done;
    }
    if (!CameraCapture_IsStopped())
    {
        result = SYS_ERR_BUSY;
        goto done;
    }
    result = s_camera.device->state == DEVICE_STATE_READY ? SYS_OK : Device_Stop(s_camera.device);
done:
    s_camera.round_stop_result = result;
    return result;
}

//建立本轮停止事实边界，不推进资源或诊断
void App_CameraBeginRound(void)
{
    CameraCaptureStopFacts_t facts;
    s_camera.round_open = true;
    s_camera.round_stop_attempted = false;
    s_camera.round_stop_result = SYS_OK;
    if (CameraCapture_GetStopFacts(&facts) == SYS_OK)
        s_camera.round_media_sequence = facts.attempt_sequence;
}

//推进Media并登记传感器停止意图
void App_CameraRun(void)
{
    CameraCaptureStopFacts_t facts;
    bool skip_error_retry = false;

    //回退仍为ERROR才跳过本轮保护重试，已排空时保留原Media推进调用
    if (s_camera.round_open && s_camera.round_stop_attempted &&
        CameraCapture_GetStopFacts(&facts) == SYS_OK)
        skip_error_retry = (facts.flags & CAMERA_STOP_ERROR) != 0U;
    if (!skip_error_retry)
        CameraCapture_Process();

    //自动停止只登记意图，普通停止始终留到Bridge推进后的轮末
    if (s_camera.initialized && CameraCapture_IsStopped() &&
        s_camera.device != NULL && s_camera.device->state == DEVICE_STATE_RUNNING)
        s_camera.stopping = true;
}

//准备传感器和Media，失败时保留已完成的准备步骤
static SysResult_t App_CameraPrepare(void)
{
	Device_t *device;
	Ov5640Device_t *camera;
	SysResult_t result;

    //相机APP自行读取默认配置，向Device和Media显式传递
    if (!s_camera.config_set)
    {
        result = App_CameraConfigure(&VideoConfig_GetDefault()->camera);
        if (result != SYS_OK)
            return result;
    }

	//公共初始化成功后，不再重复配置传感器和Media
	if ( s_camera.initialized)
		return SYS_OK;

	if ( !s_camera.configured)
	{
		//获取自动注册的摄像头设备
		device = Device_Find("ov5640");
		if ( device == NULL)
			return SYS_ERR_NOT_FOUND;
		s_camera.device = device;
		camera = FRAMEWORK_CONTAINER_OF(device, Ov5640Device_t, base);
		if ( camera->ops == NULL || camera->ops->configure == NULL)
			return SYS_ERR_NOT_SUPPORTED;

		//同一份配置用于传感器和Media
		result = camera->ops->configure(camera, App_CameraGetConfig());
		if ( result != SYS_OK)
			return result;
		s_camera.configured = true;
	}

	//Media初始化失败时，保留已完成的传感器配置供下次重试
	result = CameraCapture_Init(App_CameraGetConfig());
	if ( result != SYS_OK)
		return result;

	s_camera.initialized = true;
	return SYS_OK;
}

//先启动传感器再启动采集，失败时沿用原回退流程
static SysResult_t App_CameraStart(bool stream)
{
	SysResult_t result;
	SysResult_t stop_result;
	bool sensor_started = false;

	result = App_CameraPrepare();
	if ( result != SYS_OK)
		return result;

	//重复采集由Media拒绝，不重复启动已经运行的传感器
	if ( s_camera.device->state == DEVICE_STATE_READY)
	{
		result = Device_Start(s_camera.device);
		if ( result != SYS_OK)
			return result;
		sensor_started = true;
	}
	else if ( s_camera.device->state == DEVICE_STATE_BUSY)
		return SYS_ERR_BUSY;
	else if ( s_camera.device->state != DEVICE_STATE_RUNNING)
		return SYS_ERR_INVALID_STATE;

	result = stream ? CameraCapture_StartStream() : CameraCapture_Start();
	if ( result == SYS_OK || !sensor_started)
		return result;

	//本轮采集启动失败，先恢复Media，再停止本轮开启的传感器
	stop_result = App_CameraAdvanceStop();
	return stop_result != SYS_OK ? stop_result : result;
}

//登记只做诊断投影的观察者，不推进业务或读取硬件
void App_CameraSetDiagnosticObserver(AppCameraDiagnosticObserver_t observer)
{
    s_camera.observer = observer;
}

//保留ACK和STOP等待期间的命令接收门控
bool App_CameraCanAcceptCommand(void)
{
    return !s_camera.ack_pending && !s_camera.stop_command;
}

//处理一条已经接收的控制命令，保持原START和STOP决策
void App_CameraHandleCommand(const AppMessage_t *command)
{
    SysResult_t result;

    //空参数不构造回复，合法命令仍按原队列门控调用
    if (command == NULL)
        return;

    //先建立回复及命令诊断，再执行原命令分支
    s_camera.ack = *command;
    s_camera.ack.type = APP_MESSAGE_ACK;
    s_camera.ack.source = RUNTIME_TARGET_CAMERA;
    s_camera.ack.result = SYS_ERR_INVALID_ARG;
    s_camera.command_id = command->command_id;
    App_CameraObserve(APP_CAMERA_DIAG_COMMAND);
    if (command->type == APP_MESSAGE_START)
    {
        //运行或停止中仍回复BUSY，不创建新的采集会话
        if (s_camera.running || s_camera.stopping)
            result = SYS_ERR_BUSY;
        else
        {

            //先建立新代次，再配置并启动传感器和Media
            result = App_CameraStartResource();
        }
        //保留原错误记录和ACK结果，不额外重置已交付FAULT
        if (result != SYS_OK && result != SYS_ERR_BUSY)
            s_camera.fault = result;
        s_camera.ack.result = result;
        s_camera.ack_pending = true;
        s_camera.last_result = result;
        App_CameraObserve(APP_CAMERA_DIAG_RESULT);
    }
    else if (command->type == APP_MESSAGE_STOP)
    {

        //先停止新Offer，已有租借必须由USB完成Return握手
        s_camera.stopping = true;
        s_camera.stop_command = true;
    }
    else
        s_camera.ack_pending = true;
}

//推进Media并识别采集故障，不在这里处理Bridge和STOP回复
void App_CameraProcess(void)
{
    SysResult_t result;

    if (s_camera.prepared)
    {

        //只有CameraTask调用Media任务侧API，ISR仍使用原轮换回调
        App_CameraRun();
        result = CameraCapture_GetLastResult();
        if (result != SYS_OK && s_camera.running)
        {
            s_camera.fault = result;
            s_camera.running = false;
            s_camera.stopping = true;
            s_camera.last_result = result;
            App_CameraObserve(APP_CAMERA_DIAG_RESULT);
        }
    }

}

//仅在运行且没有停止要求时允许Bridge申请新帧
bool App_CameraCanOfferFrame(void)
{
    return s_camera.running && !s_camera.stopping;
}

//在Bridge处理后尝试停止，并按原时点更新采集统计
void App_CameraFinishRound(void)
{
    CameraCaptureStats_t stats;
    SysResult_t result;
    uint32_t now, elapsed, frames;

    if (s_camera.stopping && s_camera.round_open)
    {
        result = s_camera.prepared ? App_CameraAdvanceStop() : SYS_OK;
        //只有底层停止成功才检查Bridge，不能跳过归还确认
        if (result == SYS_OK && !App_UvcBridgeCameraIsIdle())
            result = SYS_ERR_BUSY;
        if (result == SYS_OK)
        {
            s_camera.stopping = false;
            s_camera.running = false;
            s_camera.resource_stop_confirmed = s_camera.prepared;
        }
        //请求结果只冻结一次，后续后台恢复不能把原失败改成成功
        if (s_camera.stop_result_valid && s_camera.stop_result.complete == 0U && result != SYS_ERR_BUSY)
        {
            s_camera.stop_result.result = result;
            s_camera.stop_result.hardware_stopped = result == SYS_OK && s_camera.resource_stop_confirmed;
            s_camera.stop_result.complete = 1U;
        }
        //BUSY继续等待，其他结果回复一次但失败后仍重试停止
        if (s_camera.stop_command && result != SYS_ERR_BUSY)
        {
            s_camera.ack.result = result;
            s_camera.ack_pending = true;
            s_camera.stop_command = false;
            s_camera.last_result = result;
            App_CameraObserve(APP_CAMERA_DIAG_RESULT);
        }
        if (result != SYS_OK && result != SYS_ERR_BUSY && s_camera.fault == SYS_OK)
            s_camera.fault = result;
    }
    s_camera.round_open = false;
    App_CameraObserve(APP_CAMERA_DIAG_STATE);

    //保持原统计读取及取时时点，不把配置帧率作为实测值
    if (s_camera.prepared && CameraCapture_GetStats(&stats) == SYS_OK)
    {
        s_camera.captured_frames = stats.completed_frames;
        s_camera.dropped_frames = stats.dropped_frames;
        App_CameraObserve(APP_CAMERA_DIAG_COUNTERS);
        now = Platform_TimeGetMs();
        elapsed = now - s_camera.fps_tick;
        if (elapsed >= 1000U)
        {
            frames = stats.completed_frames - s_camera.fps_frames;
            s_camera.measured_fps_milli = frames <= 4294U ? frames * 1000000U / elapsed : 0U;
            App_CameraObserve(APP_CAMERA_DIAG_FPS);
            s_camera.fps_tick = now;
            s_camera.fps_frames = stats.completed_frames;
        }
    }
}

//复制待发送ACK，读取或发送失败时不消耗原记录
bool App_CameraGetPendingAck(AppMessage_t *message)
{
    if (message == NULL || !s_camera.ack_pending)
        return false;
    *message = s_camera.ack;
    return true;
}

//仅在CameraTask确认ACK成功进入Control队列后清除待发标记
void App_CameraConfirmAckEnqueued(void)
{
    s_camera.ack_pending = false;
}

//独立构造FAULT，不受ACK是否成功入队影响
bool App_CameraGetPendingFault(AppMessage_t *message)
{
    AppMessage_t fault = {.type = APP_MESSAGE_FAULT};

    if (message == NULL || s_camera.fault == SYS_OK || s_camera.fault_sent)
        return false;
    fault.source = RUNTIME_TARGET_CAMERA;
    fault.result = s_camera.fault;
    *message = fault;
    return true;
}

//仅在FAULT成功进入Control队列后记录已经交付
void App_CameraConfirmFaultEnqueued(void)
{
    s_camera.fault_sent = true;
}

//在原状态发布位置采样运行配置，停止后保留最近应用记录
void App_CameraSampleStatus(void)
{
    const CameraConfig_t *config;

    if (s_camera.running)
    {
        config = App_CameraGetConfig();
        if (config != NULL)
        {
            s_camera.applied = *config;
            s_camera.applied_valid = true;
        }
    }
}

//读取完整业务副本，不采样配置或修改采集及停止状态
void App_CameraGetStatus(AppCameraRuntimeStatus_t *status)
{
    if (status == NULL)
        return;
    status->state = s_camera.stopping ? 2U : (s_camera.running ? 1U : 0U);
    status->command_id = s_camera.command_id;
    status->last_result = s_camera.last_result;
    status->captured_frames = s_camera.captured_frames;
    status->dropped_frames = s_camera.dropped_frames;
    status->measured_fps_milli = s_camera.measured_fps_milli;
    status->applied = s_camera.applied;
    status->applied_valid = s_camera.applied_valid;
}

//读取最近业务结果，不覆盖任务独立记录的等待错误
SysResult_t App_CameraGetLastResult(void)
{
    return s_camera.last_result;
}

//在原更新位置生成只读副本，观察者只投影指定诊断字段
static void App_CameraObserve(AppCameraDiagnosticField_t field)
{
    AppCameraRuntimeStatus_t status;

    if (s_camera.observer != NULL)
    {
        App_CameraGetStatus(&status);
        s_camera.observer(field, &status);
    }
}

//沿原路径建立资源周期，旧START和资格核心共用一次启动实现
static SysResult_t App_CameraStartResource(void)
{
    SysResult_t result;
    //记录启动窗口，旧的硬件停止确认不能用于新周期
    s_camera.resource_preparing = true;
    s_camera.resource_stop_confirmed = false;

    //保留原先先建立Bridge代次、再启动Device和Media的顺序
    result = App_UvcBridgeCameraBegin();
    if (result == SYS_OK)
        result = App_CameraInitStream();
    //部分准备失败仍由原后台停止路径继续排空
    s_camera.prepared = CameraCapture_GetLastResult() != SYS_ERR_INVALID_STATE;
    if (result == SYS_OK)
    {
        s_camera.running = true;
        s_camera.fault = SYS_OK;
        s_camera.fault_sent = false;
        s_camera.fps_tick = Platform_TimeGetMs();
        s_camera.fps_frames = 0U;
    }
    else if (s_camera.prepared)
        s_camera.stopping = true;
    s_camera.resource_preparing = false;
    return result;
}

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
