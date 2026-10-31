#include "usb_uvc.h"
#include "usb_uvc_desc.h"
#include "usb_device.h"
#include "usbd_conf.h"
#include "usbd_ioreq.h"

#include <string.h>

//class内部上下文
typedef struct
{
    uint8_t interface_alt[2];

    bool configured;
    bool ep1_opened;
    bool streaming;

    uint8_t probe[26];
    uint8_t commit[26];

} UsbUvcContext_t;

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

//绑定UVC Class操作
static USBD_ClassTypeDef s_uvc_class =
{
    
    UVC_Init,//初始化UVC上下文
    UVC_DeInit,//归还UVC上下文
    UVC_Setup,//处理控制请求
    NULL, //EP0发送完成，暂不处理
    NULL,//EP0接收完成，暂不处理
    NULL, //IN传输完成，暂不处理
    NULL,//OUT传输完成，暂不处理
    NULL,//SOF帧事件，暂不处理
    NULL,//等时IN未完成，暂不处理
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

SysResult_t USB_UVC_Register(void)
{
    return USB_Device_RegisterClass(
        &s_uvc_class,
        &s_uvc_descriptors
    );
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

    //申请归还固定内存池，实际回收由Core协调
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

    //本轮只处理标准请求
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
            //SET_INTERFACE必须为OUT请求，本轮只接受Alt 0
            if (req->bmRequest != 0x01U || req->wValue != 0U ||
                req->wLength != 0U)
                return USBD_FAIL;

            //只更新接口状态，不打开视频端点
            ctx->interface_alt[req->wIndex] = 0U;

            //零长度请求的状态阶段由Core发送
            return USBD_OK;
        }

        default:
            return USBD_FAIL;
    }
}
