//异步BOT Reset接口预检：复现现有Core/Platform行为，不实现BOT
#include "../MscEnumeration/hal_fixture.c"
#include "usb_msc_desc.h"

//模拟尚未排空的存储请求，不访问实际块设备或SD卡
static bool s_worker_active;
static uint8_t s_setup_result;

//保存各场景可观察状态，供ARM模拟执行器核对
volatile uint32_t g_reset_observation[7];

//测试Class无需数据端点，本测试仅检查EP0零长度请求分发
static uint8_t ResetTest_Init(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device;
    return config == 1U ? USBD_OK : USBD_FAIL;
}

//测试不持有真实资源
static uint8_t ResetTest_DeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device;
    (void)config;
    return USBD_OK;
}

//用不同返回值复现成功、忙和失败路径，worker状态保持不变
static uint8_t ResetTest_Setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
    (void)device;
    if (request->bmRequest != 0x21U || request->bRequest != 0xFFU ||
        request->wValue != 0U || request->wIndex != 0U || request->wLength != 0U)
        return USBD_FAIL;
    return s_setup_result;
}

static USBD_ClassTypeDef s_reset_class =
{
    .Init = ResetTest_Init,
    .DeInit = ResetTest_DeInit,
    .Setup = ResetTest_Setup,
    .GetFSConfigDescriptor = USB_MSC_GetFSConfigDescriptor
};

//每个场景由全新的模拟器内存执行，PASS仅表示复现行为符合断言
int ResetTest_Run(uint32_t scenario)
{
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_Device_RegisterClass(&s_reset_class, USB_MSC_GetDescriptors()), SYS_OK);
    EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    Configured();
    EQ(USB_Device_IsConfigured(), true);

    s_worker_active = scenario != 3U;
    s_setup_result = scenario == 1U ? USBD_BUSY : scenario == 2U ? USBD_FAIL : USBD_OK;
    Request(0x21U, 0xFFU, 0U, 0U, 0U);
    g_reset_observation[0] = s_worker_active;
    g_reset_observation[1] = s_setup_result;
    g_reset_observation[2] = s_device.ep0_state;
    g_reset_observation[3] = s_endpoints[1][0].busy;
    g_reset_observation[4] = s_endpoints[1][0].stalled;
    g_reset_observation[5] = tx_size;
    g_reset_observation[6] = stall_count;

    if (scenario == 0U || scenario == 3U)
    {
        //worker活动与否不影响Core自动提交状态，提交不是主机已接收证明
        EQ(tx_size, 0U);
        EQ(s_device.ep0_state, USBD_EP0_STATUS_IN);
        EQ(s_endpoints[1][0].busy, true);
        EQ(stall_count, 0U);
    }
    else
    {
        //BUSY和FAIL都变成STALL，不是保持NAK等待任务完成
        EQ(s_endpoints[1][0].stalled, true);
        EQ(s_endpoints[1][0].busy, false);
        EQ(stall_count > 0U, true);
        EQ(s_worker_active, true);
    }
    return 0;
}

//现有硬件桩缺少该入口，测试中只返回提交成功，不模拟真实传输
HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *device, uint8_t endpoint)
{
    (void)device;
    (void)endpoint;
    return HAL_OK;
}
