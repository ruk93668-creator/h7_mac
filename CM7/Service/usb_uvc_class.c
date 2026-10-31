#include "usb_uvc.h"
#include "usb_uvc_desc.h"
#include "usb_device.h"
#include "usbd_conf.h"

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
