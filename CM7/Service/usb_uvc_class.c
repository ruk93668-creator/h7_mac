//UVC协议与单帧异步调度，不采集图像、不管理SDRAM
#include "usb_uvc.h"
#include "usb_uvc_desc.h"
#include "usb_device.h"
#include "usb_uvc_payload.h"
#include "cmsis_compiler.h"
#include "usbd_conf.h"
#include "usbd_ioreq.h"

#include <string.h>

//UVC 1.1线上的长度，不依赖C结构体大小
#define UVC_CONTROL_SIZE 34U

//固定视频能力，与描述符保持一致，不依赖图像来源
#define UVC_VIDEO_EP_ADDRESS 0x81U
#define UVC_VIDEO_PACKET_BYTES USB_UVC_VIDEO_PACKET_SIZE

//与描述符共用冷启动配置，不保存业务配置指针
static uint32_t s_video_frame_bytes;

//记录冷启动格式，JPEG按上限校验，YUY2按固定长度校验
static CameraPixelFormat_t s_video_format;
static uint32_t s_video_interval;
static bool s_video_configured;

//累计设备侧真实EOF完成帧数，跨Class重建保留
static uint32_t s_total_completed_frames;

//按字段保存参数，不直接映射USB数据
typedef struct
{
    uint16_t bmHint;
    uint8_t bFormatIndex;
    uint8_t bFrameIndex;
    uint32_t dwFrameInterval;
    uint16_t wKeyFrameRate;
    uint16_t wPFrameRate;
    uint16_t wCompQuality;
    uint16_t wCompWindowSize;
    uint16_t wDelay;
    uint32_t dwMaxVideoFrameSize;
    uint32_t dwMaxPayloadTransferSize;
    uint32_t dwClockFrequency;
    uint8_t bmFramingInfo;
    uint8_t bPreferredVersion;
    uint8_t bMinVersion;
    uint8_t bMaxVersion;
} UvcStreamingControl_t;

//class内部上下文
typedef struct
{
    uint8_t interface_alt[2];

    bool configured;
    bool ep1_opened;
    bool streaming;

    //关闭失败后只能先重试Alt 0，不能把旧Alt 1当成正常运行
    bool ep1_close_pending;

    uint8_t probe[UVC_CONTROL_SIZE];
    uint8_t commit[UVC_CONTROL_SIZE];

    //持久EP0缓冲区，尾部补齐32位PIO读取
    uint8_t control_rx[36] __attribute__((aligned(4)));
    uint8_t control_tx[36] __attribute__((aligned(4)));

    //EP0接收选择器与协商授权，默认查询不能设置有效标志
    uint8_t pending_selector;
    bool probe_valid;
    bool probe_observed;
    bool probe_reply_pending;
    bool commit_valid;

    //包Token和底层完成序号共同拒绝迟到通知
    UsbUvcPayloadPacket_t packet;
    volatile UsbUvcStreamState_t stream_state;
    SysResult_t stream_result;
    volatile uint32_t event_token;
    uint32_t in_sequence;

    //只保存最新SOF，帧间隔从有效Commit读取
    volatile uint32_t available_sof;
    uint32_t used_sof;
    uint32_t frame_sof;
    uint32_t next_frame_sof;
    uint32_t completed_frames;
    bool paced;

} UsbUvcContext_t;

//仅指向仍存活的Class上下文，不向图像提供者公开
static UsbUvcContext_t *volatile s_uvc_active;

//跨Class释放保留Token单调性，防止固定池地址复用造成旧完成命中新流
static UsbUvcPayload_t s_uvc_payload;

//固定池容量必须满足完整Class上下文
typedef char UvcContextFitsPool[(sizeof(UsbUvcContext_t) <= 1024U) ? 1 : -1];

//IRQ只记录当前在途包的完成或错误
static uint8_t UVC_DataIn(USBD_HandleTypeDef *pdev, uint8_t ep);

//记录最新SOF，不累计发送额度
static uint8_t UVC_Sof(USBD_HandleTypeDef *pdev);

//记录ISO失败，交给所属USB任务确认Abort
static uint8_t UVC_Incomplete(USBD_HandleTypeDef *pdev, uint8_t ep);

//关闭成功后释放Payload借用，不能在Close失败时调用
static void UVC_ReleaseFrame(UsbUvcContext_t *ctx);

//初始化 UVC Class
static uint8_t UVC_Init(
    USBD_HandleTypeDef *pdev,
    uint8_t config
);

//DeInit 资源释放
static uint8_t UVC_DeInit(
    USBD_HandleTypeDef *pdev,
    uint8_t config
);

//Setup 回调
static uint8_t UVC_Setup(
    USBD_HandleTypeDef *pdev,
    USBD_SetupReqTypedef *req
);

//解码34字节小端控制数据
static bool UVC_DecodeControl(const uint8_t *data, UvcStreamingControl_t *control);

//编码34字节小端控制数据
static bool UVC_EncodeControl(const UvcStreamingControl_t *control, uint8_t *data);

//检查当前唯一视频配置
static bool UVC_ValidateControl(const UvcStreamingControl_t *control);

//协商Probe，不信任主机提供的只读字段
static bool UVC_ProbeSet(UsbUvcContext_t *ctx, const uint8_t *data);

//提交已通过GET_CUR读取的Probe结果
static bool UVC_CommitSet(UsbUvcContext_t *ctx, const uint8_t *data);

//切换VS备用设置，端点操作成功后才提交Class状态
static uint8_t UVC_SetStreamAlt(UsbUvcContext_t *ctx, uint8_t alternate);

//处理VS控制请求，不启动视频端点
static uint8_t UVC_ControlSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);

//接收并验证SET_CUR数据
static uint8_t UVC_EP0_RxReady(USBD_HandleTypeDef *pdev);

//确认Probe GET_CUR数据阶段完成
static uint8_t UVC_EP0_TxSent(USBD_HandleTypeDef *pdev);

//核对当前EP0请求，避免其他请求的完成事件使用旧授权
static bool UVC_IsControlRequest(
    const USBD_SetupReqTypedef *req,
    uint8_t direction,
    uint8_t request,
    uint8_t selector
);

//建立唯一支持的默认配置
static void UVC_DefaultControl(UvcStreamingControl_t *control);

//读取小端16位字段
static uint16_t UVC_Read16(const uint8_t *data);

//读取小端32位字段
static uint32_t UVC_Read32(const uint8_t *data);

//写入小端16位字段
static void UVC_Write16(uint8_t *data, uint16_t value);

//写入小端32位字段
static void UVC_Write32(uint8_t *data, uint32_t value);

//绑定UVC Class操作
static USBD_ClassTypeDef s_uvc_class =
{

    UVC_Init,//初始化UVC上下文
    UVC_DeInit,//归还UVC上下文
    UVC_Setup,//处理控制请求
    UVC_EP0_TxSent,//确认Probe读取完成
    UVC_EP0_RxReady,//接收Probe和Commit
    UVC_DataIn,//只记录完成Token
    NULL,//OUT传输完成，暂不处理
    UVC_Sof,//1ms机会不累计
    UVC_Incomplete,//主循环确认Abort
    NULL,//等时OUT未完成，暂不处理
    NULL,//不提供高速配置描述符
    USB_UVC_GetFSConfigDescriptor,//返回全速配置描述符
    NULL,//不提供其他速度配置描述符
    NULL//不提供设备限定描述符
};

//设备描述符回调表。
static USBD_DescriptorsTypeDef s_uvc_descriptors =
{
    USB_UVC_GetDeviceDescriptor,//设备身份保持不变
    USB_UVC_GetLangIDStrDescriptor,//索引0，支持English (United States)
    NULL,//不提供厂商字符串
    NULL,//不提供产品字符串
    NULL,//不提供序列号字符串
    NULL,//不提供配置字符串
    USB_UVC_GetInterfaceStrDescriptor//索引5，UVC Camera功能名称
};

SysResult_t USB_UVC_ValidateConfig(const VideoStreamConfig_t *config)
{
    return USB_UVC_ValidateFSConfig(config);
}

SysResult_t USB_UVC_Configure(const VideoStreamConfig_t *config)
{
    uint32_t bytes;
    SysResult_t result;

    if (__get_IPSR() != 0U || s_video_configured)
        return SYS_ERR_INVALID_STATE;
    result = USB_UVC_ValidateConfig(config);
    if (result != SYS_OK)
        return result;
    result = USB_UVC_ConfigureFSDescriptor(config);
    if (result != SYS_OK)
        return result;
    if (config->camera.format == CAMERA_PIXEL_FORMAT_JPEG)
        bytes = config->max_frame_bytes;
    else
        (void)CameraConfig_GetFrameSize(&config->camera, &bytes);
    s_video_frame_bytes = bytes;
    s_video_format = config->camera.format;
    s_video_interval = 10000000UL / config->fps;
    s_video_configured = true;
    return SYS_OK;
}

SysResult_t USB_UVC_Register(void)
{
    if (!s_video_configured)
        return SYS_ERR_INVALID_STATE;
    return USB_Device_RegisterClass(
        &s_uvc_class,
        &s_uvc_descriptors
    );
}

//借用外部帧，成功后等待状态查询确认归还
SysResult_t USB_UVC_SubmitFrame(const uint8_t *frame, uint32_t length)
{
    uint32_t mask;
    SysResult_t result;
    UsbUvcContext_t *ctx;

    //固定图像长度必须一致，JPEG只接收上限内的完整文件
    if (frame == NULL || length == 0U ||
        (s_video_format == CAMERA_PIXEL_FORMAT_JPEG ?
         length > s_video_frame_bytes : length != s_video_frame_bytes))
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;

    //先检查地址加法，禁止尾部标记检查绕过Payload的溢出保护
    if ((uintptr_t)frame > UINTPTR_MAX - (length - 1U))
        return SYS_ERR_INVALID_ARG;

    //边界检查在任务中完成，UVC不扫描或解码摄像头图像
    if (s_video_format == CAMERA_PIXEL_FORMAT_JPEG &&
        (length < 4U || frame[0] != 0xFFU || frame[1] != 0xD8U ||
         frame[length - 2U] != 0xFFU || frame[length - 1U] != 0xD9U))
        return SYS_ERR_INVALID_ARG;
    mask = USB_Device_Lock();
    ctx = s_uvc_active;
    if (ctx == NULL || !USB_Device_IsConfigured() || !ctx->commit_valid ||
        !ctx->ep1_opened || ctx->interface_alt[1] != 1U || ctx->ep1_close_pending)
        result = SYS_ERR_INVALID_STATE;
    else if (ctx->stream_state != USB_UVC_WAIT_FRAME) result = SYS_ERR_BUSY;
    else
    {
        if (s_uvc_payload.state == USB_UVC_PAYLOAD_UNINITIALIZED)
            (void)USB_UVC_PayloadInit(&s_uvc_payload);
        result = USB_UVC_PayloadStartFrame(&s_uvc_payload, frame, length, UVC_VIDEO_PACKET_BYTES);
        if (result == SYS_OK)
        {
            ctx->stream_state = USB_UVC_WAIT_SLOT;
            ctx->stream_result = SYS_OK;
            ctx->used_sof = USB_Device_GetSofSequence();
        }
    }
    USB_Device_Unlock(mask);
    return result;
}

//登记取消，所属USB任务确认Abort后才释放借用
SysResult_t USB_UVC_CancelFrame(void)
{
    uint32_t mask;
    UsbUvcContext_t *ctx;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    mask = USB_Device_Lock();
    ctx = s_uvc_active;
    if (ctx == NULL)
    {
        USB_Device_Unlock(mask);
        return SYS_ERR_INVALID_STATE;
    }
    if (ctx->stream_state != USB_UVC_WAIT_FRAME)
        ctx->stream_state = USB_UVC_CANCELLING;
    USB_Device_Unlock(mask);
    return SYS_OK;
}

//在USB临界区读取一致状态，不向上层暴露Class对象
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *status)
{
    uint32_t mask;
    UsbUvcContext_t *ctx;
    if (status == NULL)
        return SYS_ERR_INVALID_ARG;
    if (__get_IPSR() != 0U)
        return SYS_ERR_INVALID_STATE;
    mask = USB_Device_Lock();
    ctx = s_uvc_active;
    if (ctx == NULL)
    {
        USB_Device_Unlock(mask);
        return SYS_ERR_INVALID_STATE;
    }
    status->state = ctx->stream_state;
    status->result = ctx->stream_result;
    status->frame_offset = s_uvc_payload.frame_offset;
    status->token = ctx->packet.token;
    status->frame_owned = s_uvc_payload.frame != NULL;

    //在同一个USB临界区内查询，Application不直接读取Class内部状态
    status->can_submit = USB_Device_IsConfigured() && ctx->commit_valid &&
        ctx->ep1_opened && ctx->interface_alt[1] == 1U && !ctx->ep1_close_pending &&
        ctx->stream_state == USB_UVC_WAIT_FRAME && !status->frame_owned;
    status->completed_frames = ctx->completed_frames;
    USB_Device_Unlock(mask);
    return SYS_OK;
}

//所属USB任务处理包构造、完成确认和取消恢复
void USB_UVC_Process(void)
{
    UsbUvcContext_t *ctx;
    SysResult_t result;
    uint32_t mask, sof;
    if (__get_IPSR() != 0U)
        return;
    mask = USB_Device_Lock();
    ctx = s_uvc_active;
    if (ctx == NULL)
    {
        USB_Device_Unlock(mask);
        return;
    }

    //取消和故障优先，失败时保留帧引用以便重试
    if (ctx->stream_state == USB_UVC_CANCELLING || ctx->stream_state == USB_UVC_RECOVERY)
    {
        result = USB_UVC_PayloadCancel(&s_uvc_payload);
        if (result == SYS_ERR_BUSY)
        {
            result = USB_Device_AbortEp(UVC_VIDEO_EP_ADDRESS);
            if (result == SYS_OK) UVC_ReleaseFrame(ctx);
            else
            {
                ctx->stream_result = result;
                ctx->stream_state = USB_UVC_RECOVERY;
            }
        }
        else if (result == SYS_OK || s_uvc_payload.state == USB_UVC_PAYLOAD_UNINITIALIZED)
            UVC_ReleaseFrame(ctx);
        USB_Device_Unlock(mask); return;
    }
    if (!USB_Device_IsConfigured() || !ctx->ep1_opened || ctx->ep1_close_pending || !ctx->commit_valid)
    {
        USB_Device_Unlock(mask);
        return;
    }

    //真实完成只在这里提交偏移，末包完成才归还整帧
    if (ctx->stream_state == USB_UVC_WAIT_CONFIRM)
    {
        if (ctx->event_token != ctx->packet.token || ctx->event_token == 0U)
        {
            ctx->stream_result = SYS_ERR_PROTOCOL;
            ctx->stream_state = USB_UVC_RECOVERY;
            USB_Device_Unlock(mask);
            return;
        }
        result = USB_UVC_PayloadCompletePacket(&s_uvc_payload, ctx->event_token);
        ctx->event_token = 0U;
        if (result != SYS_OK)
        {
            ctx->stream_result = result;
            ctx->stream_state = USB_UVC_RECOVERY;
        }
        else if (s_uvc_payload.state == USB_UVC_PAYLOAD_IDLE)
        {
            ctx->completed_frames++;
            s_total_completed_frames++;
            ctx->streaming = false;
            ctx->stream_state = USB_UVC_WAIT_FRAME;
        }
        else ctx->stream_state = USB_UVC_WAIT_SLOT;
    }

    //每个SOF最多一次机会，帧间隔沿用Commit
    sof = ctx->available_sof;
    if (ctx->stream_state != USB_UVC_WAIT_SLOT || sof == ctx->used_sof ||
        (ctx->paced && (int32_t)(sof - ctx->next_frame_sof) < 0 && s_uvc_payload.frame_offset == 0U))
    {
        USB_Device_Unlock(mask);
        return;
    }
    ctx->used_sof = sof;
    result = USB_UVC_PayloadBuildPacket(&s_uvc_payload, &ctx->packet);
    if (result != SYS_OK)
    {
        ctx->stream_result = result;
        ctx->stream_state = USB_UVC_RECOVERY;
        USB_Device_Unlock(mask);
        return;
    }

    //在使能USB发送之前保存Token与在途状态，防止首个事件先到
    ctx->event_token = 0U;
    ctx->in_sequence = USB_Device_GetInSequence(UVC_VIDEO_EP_ADDRESS) + 1U;
    ctx->stream_state = USB_UVC_IN_FLIGHT;
    result = USB_Device_Transmit(UVC_VIDEO_EP_ADDRESS, (uint8_t *)ctx->packet.data, ctx->packet.length);
    if (result == SYS_OK)
    {
        ctx->streaming = true;
        if (s_uvc_payload.frame_offset == 0U)
        {
            ctx->frame_sof = sof;
            ctx->next_frame_sof = sof + UVC_Read32(ctx->commit + 4) / 10000U;
            ctx->paced = true;
        }
    }
    else if (result == SYS_ERR_BUSY)
        ctx->stream_state = USB_UVC_WAIT_SLOT;
    else
    {
        ctx->stream_result = result;
        ctx->stream_state = USB_UVC_RECOVERY;
    }
    USB_Device_Unlock(mask);
}

//内部功能函数实现

//初始化UVC上下文，两个接口默认使用Alt 0
static uint8_t UVC_Init(
    USBD_HandleTypeDef *pdev,
    uint8_t config
)
{
    UsbUvcContext_t *ctx;

    //检查设备句柄和配置编号
    if (pdev == NULL || config != 1U)
        return USBD_FAIL;

    //已有上下文时拒绝重复初始化
    if (pdev->pUserData[0] != NULL)
        return USBD_FAIL;

    //从USB固定内存池申请上下文
    ctx = USBD_malloc(sizeof(UsbUvcContext_t));
    if (ctx == NULL)
        return USBD_EMEM;

    //清零接口Alt及其他运行状态
    memset(ctx, 0, sizeof(UsbUvcContext_t));

	// SET_CONFIGURATION成功后进入已配置状态
	ctx->configured = true;

    //保存上下文，供后续UVC回调使用
    pdev->pUserData[0] = ctx;
    s_uvc_active = ctx;

    return USBD_OK;
}

//归还UVC上下文，允许重复释放
static uint8_t UVC_DeInit(
    USBD_HandleTypeDef *pdev,
    uint8_t config
)
{
    UsbUvcContext_t *ctx;

    //释放上下文不依赖配置编号
    (void)config;

    //检查设备句柄
    if (pdev == NULL)
        return USBD_FAIL;

    //取得当前UVC上下文，空上下文无需重复释放
    ctx = pdev->pUserData[0];
    if (ctx == NULL)
        return USBD_OK;

    //先关闭视频端点，失败时保留上下文及缓冲区所有权
    if (ctx->ep1_opened && UVC_SetStreamAlt(ctx, 0U) != USBD_OK)
    {

        //官方Core忽略取消配置时的DeInit错误，显式STALL阻止成功状态包
        if (pdev->ep0_state == USBD_EP0_SETUP &&
            pdev->request.bmRequest == 0x00U &&
            pdev->request.bRequest == USB_REQ_SET_CONFIGURATION)
            USBD_CtlError(pdev, &pdev->request);
        return USBD_FAIL;
    }

    //申请归还固定内存池，实际回收由Core协调
    if (s_uvc_active == ctx) s_uvc_active = NULL;
    USBD_free(ctx);

    //清除绑定，防止后续回调访问旧上下文
    pdev->pUserData[0] = NULL;

    return USBD_OK;
}

static uint8_t UVC_Setup(
    USBD_HandleTypeDef *pdev,
    USBD_SetupReqTypedef *req
)
{
    UsbUvcContext_t *ctx;

    //检查设备句柄和请求指针
    if (pdev == NULL || req == NULL)
        return USBD_FAIL;

    //接口请求只能在已配置且上下文有效时处理
    ctx = pdev->pUserData[0];
    if (ctx == NULL || !ctx->configured ||
        pdev->dev_state != USBD_STATE_CONFIGURED)
        return USBD_FAIL;

    //新SETUP取消上一笔未完成的数据阶段
    ctx->pending_selector = 0U;
    ctx->probe_reply_pending = false;

    //VS类请求独立分发
    if ((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS)
        return UVC_ControlSetup(pdev, req);

    //其余仅处理标准请求
    if ((req->bmRequest & USB_REQ_TYPE_MASK) != USB_REQ_TYPE_STANDARD)
        return USBD_FAIL;

    //接收对象必须是接口，不能忽略保留位
    if ((req->bmRequest & 0x1FU) != USB_REQ_RECIPIENT_INTERFACE)
        return USBD_FAIL;

    //检查完整接口编号，拒绝非零高字节
    if (req->wIndex > 1U)
        return USBD_FAIL;

    switch (req->bRequest)
    {
        case USB_REQ_GET_STATUS:
        {

            //接口0和1均无状态位，严格返回两个零字节
            if (req->bmRequest != 0x81U || req->wValue != 0U ||
                req->wLength != 2U)
                return USBD_FAIL;

            //沿用持久响应缓冲区，清零尾部保证PIO读取安全
            memset(ctx->control_tx, 0, sizeof(ctx->control_tx));
            return USBD_CtlSendData(pdev, ctx->control_tx, 2U);
        }

        case USB_REQ_GET_INTERFACE:
        {

            //GET_INTERFACE必须为IN请求，返回一个字节
            if (req->bmRequest != 0x81U || req->wValue != 0U ||
                req->wLength != 1U)
                return USBD_FAIL;

            //使用上下文中的持久数据，不能发送栈上临时变量
            return USBD_CtlSendData(pdev,
                &ctx->interface_alt[req->wIndex], 1U);
        }

        case USB_REQ_SET_INTERFACE:
        {

            //完整检查OUT请求和Alt，不能忽略wValue高字节
            if (req->bmRequest != 0x01U || req->wValue > 1U ||
                req->wLength != 0U)
                return USBD_FAIL;

            //VC接口0只支持Alt 0，不影响VS端点
            if (req->wIndex == 0U)
            {
                if (req->wValue != 0U)
                    return USBD_FAIL;
                ctx->interface_alt[0] = 0U;
                return USBD_OK;
            }

            //成功才由Core发送状态阶段，失败交由Core STALL
            return UVC_SetStreamAlt(ctx, (uint8_t)req->wValue);
        }

        default:
            return USBD_FAIL;
    }
}

//处理Table 4-48和4-49定义的VS请求
static uint8_t UVC_ControlSetup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    UsbUvcContext_t *ctx = pdev->pUserData[0];
    UvcStreamingControl_t control;
    uint8_t selector = (uint8_t)(req->wValue >> 8);
    uint16_t length = UVC_CONTROL_SIZE;
    uint8_t status;

    //只接受VS接口1，控制通道低字节为0
    if (req->wIndex != 1U || (req->wValue & 0xFFU) != 0U ||
        (selector != 1U && selector != 2U))
        return USBD_FAIL;

    if (req->bRequest == 0x01U)
    {

        //只接收34字节结构
        if (req->bmRequest != 0x21U || req->wLength != UVC_CONTROL_SIZE)
            return USBD_FAIL;
        memset(ctx->control_rx, 0, sizeof(ctx->control_rx));
        status = (uint8_t)USBD_CtlPrepareRx(pdev, ctx->control_rx, UVC_CONTROL_SIZE);
        if (status == USBD_OK)
            ctx->pending_selector = selector;
        return status;
    }

    //GET必须为接口IN请求，不能返回超过主机要求的长度
    if (req->bmRequest != 0xA1U || req->wLength == 0U)
        return USBD_FAIL;
    memset(ctx->control_tx, 0, sizeof(ctx->control_tx));
    switch (req->bRequest)
    {
        case 0x81U:
        {

            // 有效协商存在时，返回真实协商值
            if ((selector == 1U && ctx->probe_valid) ||
                (selector == 2U && ctx->commit_valid))
            {
                memcpy(ctx->control_tx,
                       selector == 1U ? ctx->probe : ctx->commit,
                       UVC_CONTROL_SIZE);
            }
            else
            {

                // 未协商时，返回设备支持的默认值
                UVC_DefaultControl(&control);
                (void)UVC_EncodeControl(&control, ctx->control_tx);
            }

            break;
        }
        case 0x82U:
        case 0x83U:
        case 0x87U:

            //唯一能力使MIN、MAX、DEF一致
            if (selector != 1U)
                return USBD_FAIL;
            UVC_DefaultControl(&control);
            UVC_EncodeControl(&control, ctx->control_tx);
            break;

        case 0x84U:

            //固定能力无可调步长，各字段分辨率为0
            if (selector != 1U)
                return USBD_FAIL;
            break;

        case 0x85U:
            UVC_Write16(ctx->control_tx, UVC_CONTROL_SIZE);
            length = 2U;
            break;

        case 0x86U:

            //GET_INFO：GET和SET能力位置1，其余位为0
            ctx->control_tx[0] = 0x03U;
            length = 1U;
            break;

        default:
            return USBD_FAIL;
    }
    if (length > req->wLength)
        length = req->wLength;
    status = (uint8_t)USBD_CtlSendData(pdev, ctx->control_tx, length);

	// 只有真实有效的Probe读取才能授权后续Commit
	if (status == USBD_OK &&
		selector == 1U &&
		req->bRequest == 0x81U &&
		length == UVC_CONTROL_SIZE &&
		ctx->probe_valid)
	{
		ctx->probe_reply_pending = true;
	}

    return status;
}

//SET_CUR数据阶段结束后才能更新控制状态
static uint8_t UVC_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
    UsbUvcContext_t *ctx;
    uint8_t selector;
    bool valid;
    if (pdev == NULL || pdev->dev_state != USBD_STATE_CONFIGURED)
        return USBD_FAIL;
    ctx = pdev->pUserData[0];
    if (ctx == NULL || !ctx->configured || ctx->pending_selector == 0U)
        return USBD_FAIL;
    selector = ctx->pending_selector;

    //不论当前回调属于谁，旧接收授权都只能消费一次
    ctx->pending_selector = 0U;

    //设备级请求不会进入UVC_Setup，必须在完成回调再次核对事务
    if (!UVC_IsControlRequest(&pdev->request, 0x21U, 0x01U, selector) ||
        pdev->ep0_state != USBD_EP0_DATA_OUT ||
        pdev->ep_out[0].total_length != UVC_CONTROL_SIZE)
        return USBD_FAIL;

    valid = USBD_GetRxCount(pdev, 0U) == UVC_CONTROL_SIZE;
    if (valid)
        valid = selector == 1U ? UVC_ProbeSet(ctx, ctx->control_rx) :
            UVC_CommitSet(ctx, ctx->control_rx);
    if (!valid)
    {

        //Probe数据不完整时，旧协商结果也不能继续授权Commit
        if (selector == 1U)
        {
            ctx->probe_valid = false;
            ctx->probe_observed = false;
            ctx->probe_reply_pending = false;
        }

        //Core忽略返回值，显式STALL，由Platform阻止状态包
        USBD_CtlError(pdev, &pdev->request);
        return USBD_FAIL;
    }
    return USBD_OK;
}

//Core完成整段EP0 IN数据后调用
static uint8_t UVC_EP0_TxSent(USBD_HandleTypeDef *pdev)
{
    UsbUvcContext_t *ctx;
    if (pdev == NULL || pdev->dev_state != USBD_STATE_CONFIGURED)
        return USBD_FAIL;
    ctx = pdev->pUserData[0];
    if (ctx == NULL || !ctx->configured)
        return USBD_FAIL;
    if (ctx->probe_reply_pending)
    {

        //其他设备请求也会触发本回调，只有完整Probe GET_CUR才能授权
        if (ctx->probe_valid &&
            UVC_IsControlRequest(&pdev->request, 0xA1U, 0x81U, 1U) &&
            pdev->ep0_state == USBD_EP0_DATA_IN &&
            pdev->ep_in[0].total_length == UVC_CONTROL_SIZE)
            ctx->probe_observed = true;

        //完成事件与旧授权不匹配时仅取消授权，不改动协商或提交值
        ctx->probe_reply_pending = false;
    }
    return USBD_OK;
}

//不支持的Probe值归一为描述符已声明的能力
static bool UVC_ProbeSet(UsbUvcContext_t *ctx, const uint8_t *data)
{
    UvcStreamingControl_t requested;
    UvcStreamingControl_t negotiated;
    if (ctx == NULL || !UVC_DecodeControl(data, &requested))
        return false;

    //新一轮协商不能继续使用上次的读取授权
    ctx->probe_valid = false;
    ctx->probe_observed = false;
    ctx->probe_reply_pending = false;

    //唯一配置无法满足更低带宽时，不能反向增加数据率
    if (requested.dwFrameInterval > s_video_interval ||
        (requested.dwMaxPayloadTransferSize != 0U && requested.dwMaxPayloadTransferSize < UVC_VIDEO_PACKET_BYTES))
        return false;

    //全零版本表示重新协商，其他版本必须属于已支持范围
    if ((requested.bPreferredVersion != 0U || requested.bMinVersion != 0U ||
        requested.bMaxVersion != 0U) &&
        (requested.bPreferredVersion != 1U || requested.bMinVersion != 1U ||
        requested.bMaxVersion != 1U))
        return false;
    UVC_DefaultControl(&negotiated);

    //只保留未压缩格式适用的帧间隔提示位
    negotiated.bmHint = requested.bmHint & 0x01U;
    if (!UVC_ValidateControl(&negotiated))
        return false;
    UVC_EncodeControl(&negotiated, ctx->probe);
    ctx->probe_valid = true;
    ctx->probe_observed = false;
    ctx->probe_reply_pending = false;
    return true;
}

//Commit必须完整匹配已读取的Probe，不再协商
static bool UVC_CommitSet(UsbUvcContext_t *ctx, const uint8_t *data)
{
    UvcStreamingControl_t control;
    if (ctx == NULL || !ctx->probe_valid || !ctx->probe_observed ||
        !UVC_DecodeControl(data, &control) || !UVC_ValidateControl(&control))
        return false;
    if (memcmp(data, ctx->probe, UVC_CONTROL_SIZE) != 0)
        return false;
    UVC_EncodeControl(&control, ctx->commit);
    ctx->commit_valid = true;
    return true;
}

//唯一的视频能力，时钟与旧VC Header无关
static void UVC_DefaultControl(UvcStreamingControl_t *control)
{
    memset(control, 0, sizeof(*control));
    control->bFormatIndex = 1U;
    control->bFrameIndex = 1U;
    control->dwFrameInterval = s_video_interval;
    control->dwMaxVideoFrameSize = s_video_frame_bytes;

    //FS单个等时事务上限字节，包含Payload头
    control->dwMaxPayloadTransferSize = UVC_VIDEO_PACKET_BYTES;

    //协商声明1kHz时基，当前Payload不携带PTS或SCR
    control->dwClockFrequency = 1000UL;

    //未压缩帧使用FID和EOF，不启用PTS或SCR
    control->bmFramingInfo = 3U;

    //未压缩Payload规范Revision 1.1，minor版本为1
    control->bPreferredVersion = 1U;
    control->bMinVersion = 1U;
    control->bMaxVersion = 1U;
}

//检查Commit能否由当前能力满足
static bool UVC_ValidateControl(const UvcStreamingControl_t *control)
{
    if (control == NULL)
        return false;
    return (control->bmHint & ~1U) == 0U &&
        control->bFormatIndex == 1U && control->bFrameIndex == 1U &&
        control->dwFrameInterval == s_video_interval &&
        control->wKeyFrameRate == 0U && control->wPFrameRate == 0U &&
        control->wCompQuality == 0U && control->wCompWindowSize == 0U &&
        control->wDelay == 0U && control->dwMaxVideoFrameSize == s_video_frame_bytes &&
        control->dwMaxPayloadTransferSize == UVC_VIDEO_PACKET_BYTES &&
        control->dwClockFrequency == 1000UL && control->bmFramingInfo == 3U &&
        control->bPreferredVersion == 1U && control->bMinVersion == 1U &&
        control->bMaxVersion == 1U;
}

//逐字段解码，禁止把USB缓冲区强制转换为结构体
static bool UVC_DecodeControl(const uint8_t *data, UvcStreamingControl_t *control)
{
    if (data == NULL || control == NULL)
        return false;
    control->bmHint = UVC_Read16(data);
    control->bFormatIndex = data[2];
    control->bFrameIndex = data[3];
    control->dwFrameInterval = UVC_Read32(data + 4);
    control->wKeyFrameRate = UVC_Read16(data + 8);
    control->wPFrameRate = UVC_Read16(data + 10);
    control->wCompQuality = UVC_Read16(data + 12);
    control->wCompWindowSize = UVC_Read16(data + 14);
    control->wDelay = UVC_Read16(data + 16);
    control->dwMaxVideoFrameSize = UVC_Read32(data + 18);
    control->dwMaxPayloadTransferSize = UVC_Read32(data + 22);
    control->dwClockFrequency = UVC_Read32(data + 26);
    control->bmFramingInfo = data[30];
    control->bPreferredVersion = data[31];
    control->bMinVersion = data[32];
    control->bMaxVersion = data[33];
    return true;
}

//编码只写有效34字节，PIO尾部由持久缓冲区补齐
static bool UVC_EncodeControl(const UvcStreamingControl_t *control, uint8_t *data)
{
    if (control == NULL || data == NULL)
        return false;
    UVC_Write16(data, control->bmHint);
    data[2] = control->bFormatIndex;
    data[3] = control->bFrameIndex;
    UVC_Write32(data + 4, control->dwFrameInterval);
    UVC_Write16(data + 8, control->wKeyFrameRate);
    UVC_Write16(data + 10, control->wPFrameRate);
    UVC_Write16(data + 12, control->wCompQuality);
    UVC_Write16(data + 14, control->wCompWindowSize);
    UVC_Write16(data + 16, control->wDelay);
    UVC_Write32(data + 18, control->dwMaxVideoFrameSize);
    UVC_Write32(data + 22, control->dwMaxPayloadTransferSize);
    UVC_Write32(data + 26, control->dwClockFrequency);
    data[30] = control->bmFramingInfo;
    data[31] = control->bPreferredVersion;
    data[32] = control->bMinVersion;
    data[33] = control->bMaxVersion;
    return true;
}

static uint16_t UVC_Read16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t UVC_Read32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void UVC_Write16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void UVC_Write32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

//完整核对方向、类型、对象、操作、接口、selector和长度
static bool UVC_IsControlRequest(
    const USBD_SetupReqTypedef *req,
    uint8_t direction,
    uint8_t request,
    uint8_t selector
)
{
    if (req == NULL || (selector != 1U && selector != 2U))
        return false;
    if (req->bmRequest != direction || req->bRequest != request ||
        req->wIndex != 1U || req->wValue != ((uint16_t)selector << 8))
        return false;

    //SET_CUR只允许34字节，GET_CUR允许主机给出更大的接收上限
    if (direction == 0x21U)
        return req->wLength == UVC_CONTROL_SIZE;
    return req->wLength >= UVC_CONTROL_SIZE;
}

//只管理视频端点生命周期，打开端点不等于开始发送
static uint8_t UVC_SetStreamAlt(UsbUvcContext_t *ctx, uint8_t alternate)
{
    SysResult_t result;

    if (alternate == 0U)
    {

        //先禁止新提交，即使Close失败也不能恢复发送
        ctx->stream_state = USB_UVC_CANCELLING;
        if (ctx->ep1_opened)
        {

            //Close已包含取消和FIFO清理，不额外重复调用Abort
            result = USB_Device_CloseEp(UVC_VIDEO_EP_ADDRESS);
            if (result != SYS_OK)
            {

                //保留Alt和端点所有权，允许主机重试Alt 0
                ctx->ep1_close_pending = true;
                return USBD_FAIL;
            }
        }

        UVC_ReleaseFrame(ctx);
        ctx->paced = false;

        //实际关闭成功后再发布Alt 0
        ctx->ep1_opened = false;
        ctx->ep1_close_pending = false;
        ctx->interface_alt[1] = 0U;
        ctx->streaming = false;
        return USBD_OK;
    }

    //进入Alt 1必须已有Commit，关闭故障必须先恢复
    if (alternate != 1U || !ctx->commit_valid || ctx->ep1_close_pending)
        return USBD_FAIL;

    //同一Alt重复请求不重复申请端点
    if (ctx->ep1_opened)
        return ctx->interface_alt[1] == 1U ? USBD_OK : USBD_FAIL;

    //参数与当前描述符EP1 IN等时字节一致
    result = USB_Device_OpenEp(UVC_VIDEO_EP_ADDRESS, USBD_EP_TYPE_ISOC, UVC_VIDEO_PACKET_BYTES);
    if (result != SYS_OK)
        return USBD_FAIL;

    if (s_uvc_payload.state == USB_UVC_PAYLOAD_IDLE)
        (void)USB_UVC_PayloadReset(&s_uvc_payload);

    //打开成功只代表资源就绪，没有外部帧就不发送
    ctx->available_sof = USB_Device_GetSofSequence();
    ctx->used_sof = ctx->available_sof;
    ctx->ep1_opened = true;
    ctx->interface_alt[1] = 1U;
    ctx->streaming = false;
    return USBD_OK;
}

//数据完成IRQ只保存Token，生命周期IRQ在Close确认后释放Payload
static uint8_t UVC_DataIn(USBD_HandleTypeDef *pdev, uint8_t ep)
{
    UsbUvcContext_t *ctx;
    if (pdev == NULL) return USBD_FAIL;
    ctx = pdev->pUserData[0];
    if (ep == 1U && ctx != NULL && ctx == s_uvc_active &&
        ctx->stream_state == USB_UVC_IN_FLIGHT && ctx->ep1_opened &&
        ctx->in_sequence == USB_Device_GetInCompleteSequence(UVC_VIDEO_EP_ADDRESS))
    {
        ctx->event_token = ctx->packet.token;
        ctx->stream_state = USB_UVC_WAIT_CONFIRM;
    }
    return USBD_OK;
}

static uint8_t UVC_Sof(USBD_HandleTypeDef *pdev)
{
    UsbUvcContext_t *ctx;
    if (pdev == NULL) return USBD_FAIL;
    ctx = pdev->pUserData[0];

    //只覆盖最新机会，不累计额度，也不构包或发送
    if (ctx != NULL && ctx == s_uvc_active && ctx->ep1_opened && ctx->interface_alt[1] == 1U)
        ctx->available_sof = USB_Device_GetSofSequence();
    return USBD_OK;
}

static uint8_t UVC_Incomplete(USBD_HandleTypeDef *pdev, uint8_t ep)
{
    UsbUvcContext_t *ctx;
    if (pdev == NULL) return USBD_FAIL;
    ctx = pdev->pUserData[0];
    if (ep == 1U && ctx != NULL && ctx == s_uvc_active &&
        (ctx->stream_state == USB_UVC_IN_FLIGHT || ctx->stream_state == USB_UVC_WAIT_CONFIRM))
    {
        ctx->event_token = 0U;
        ctx->stream_result = SYS_ERR_TIMEOUT;
        ctx->stream_state = USB_UVC_RECOVERY;
    }
    return USBD_OK;
}

static void UVC_ReleaseFrame(UsbUvcContext_t *ctx)
{

    //无在途包，或Close/Abort已确认停止后，才允许清理借用
    if (s_uvc_payload.state != USB_UVC_PAYLOAD_UNINITIALIZED)
    {
        if (USB_UVC_PayloadCancel(&s_uvc_payload) == SYS_ERR_BUSY)
            (void)USB_UVC_PayloadAbortPacket(&s_uvc_payload, s_uvc_payload.pending_token);
    }
    ctx->event_token = 0U;
    memset(&ctx->packet, 0, sizeof(ctx->packet));
    ctx->stream_state = USB_UVC_WAIT_FRAME;
    ctx->streaming = false;
}

//查询设备侧累计EOF完成数
uint32_t USB_UVC_GetCompletedFrameCount(void)
{

    //由UsbTask读取，计数只在真实末包确认后递增
    return s_total_completed_frames;
}
