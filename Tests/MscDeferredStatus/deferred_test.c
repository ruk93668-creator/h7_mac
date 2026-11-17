//真实Core/Platform控制事务测试，使用现有HAL桩
#include "../MscEnumeration/hal_fixture.c"
#include "usb_msc_desc.h"
static uint32_t s_case, s_token;
static SysResult_t s_defer_result, s_duplicate;
static bool s_worker_owns_buffer;
static uint8_t DeferredTest_Init(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device;
    return config == 1U ? USBD_OK : USBD_FAIL;
}
static uint8_t DeferredTest_DeInit(USBD_HandleTypeDef *device, uint8_t config)
{
    (void)device;
    (void)config;
    return USBD_OK;
}
static uint8_t DeferredTest_Setup(USBD_HandleTypeDef *device, USBD_SetupReqTypedef *request)
{
    USBD_SetupReqTypedef copy = *request;
    (void)device;
    if (s_case == 8U)
        copy.wIndex++;
    s_defer_result = USB_Device_DeferControlStatus(&copy, &s_token);
    if (s_case == 9U)
        s_duplicate = USB_Device_DeferControlStatus(request, &s_token);
    return s_case == 10U ? USBD_OK : USBD_BUSY;
}
static USBD_ClassTypeDef s_driver =
{
    .Init = DeferredTest_Init,
    .DeInit = DeferredTest_DeInit,
    .Setup = DeferredTest_Setup,
    .GetFSConfigDescriptor = USB_MSC_GetFSConfigDescriptor
};
int DeferredTest_Run(uint32_t scenario)
{
    UsbControlStatus_t status;
    uint32_t previous;
    s_case = scenario;
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);
    RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(), SYS_OK);
    EQ(USB_Device_RegisterClass(&s_driver, USB_MSC_GetDescriptors()), SYS_OK);
    EQ(USB_Device_Start(), SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    Configured();
    EQ(USB_Device_IsConfigured(), true);
    EQ(USB_Device_DeferControlStatus(&s_device.request, &s_token), SYS_ERR_INVALID_STATE);
    s_worker_owns_buffer = true;
    if (scenario == 7U)
        s_control_token_seed = UINT32_MAX;
    Request(0x21U, 0xFFU, 0U, 0U, 0U);
    if (scenario == 7U || scenario == 8U)
    {
        EQ(s_defer_result, scenario == 7U ? SYS_ERR_NO_RESOURCE : SYS_ERR_INVALID_STATE);
        EQ(s_endpoints[1][0].stalled, true);
        EQ(s_token, 0U);
        return 0;
    }
    if (scenario == 10U)
    {
        EQ(s_endpoints[1][0].stalled, true);
        EQ(tx_size, 999U);
        EQ(USB_Device_CompleteControlStatus(s_token), SYS_ERR_INVALID_STATE);
        return 0;
    }
    EQ(s_defer_result, SYS_OK);
    EQ(stall_count, 0U);
    EQ(tx_size, 999U);
    EQ(USB_Device_GetControlStatus(s_token, &status), SYS_OK);
    EQ(status, USB_CONTROL_WAITING);
    if (scenario == 9U)
        EQ(s_duplicate, SYS_ERR_INVALID_STATE);
    if (scenario == 1U)
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);
    if (scenario == 2U)
        HAL_PCD_ResetCallback(&s_pcd);
    if (scenario == 3U)
        HAL_PCD_DisconnectCallback(&s_pcd);
    if (scenario == 4U || scenario == 11U)
    {
        if (scenario == 11U)
            fault = 8U;
        EQ(USB_Device_Stop(), scenario == 11U ? SYS_ERR_IO : SYS_OK);
    }
    if (scenario == 5U)
    {
        EQ(USB_Device_FailControlStatus(s_token), SYS_OK);
        EQ(USB_Device_GetControlStatus(s_token, &status), SYS_OK);
        EQ(status, USB_CONTROL_FAILED);
        EQ(USB_Device_CompleteControlStatus(s_token), SYS_ERR_INVALID_STATE);
        EQ(s_worker_owns_buffer, true);
        return 0;
    }
    if (scenario == 6U)
        fault = 9U;
    if (scenario == 12U)
        EQ(USBD_DeInit(&s_device), USBD_OK);
    if ((scenario >= 1U && scenario <= 4U) || scenario == 11U || scenario == 12U)
    {
        EQ(USB_Device_CompleteControlStatus(s_token), SYS_ERR_INVALID_STATE);
        EQ(USB_Device_GetControlStatus(s_token, &status), SYS_OK);
        EQ(status, USB_CONTROL_CANCELLED);
        EQ(s_worker_owns_buffer, true);
        return 0;
    }
    if (scenario == 13U)
    {
        previous = s_token;
        Request(0x21U, 0xFFU, 0U, 0U, 0U);
        EQ(s_token > previous, true);
        EQ(USB_Device_CompleteControlStatus(previous), SYS_ERR_INVALID_STATE);
    }
    if (scenario == 15U)
        return 0;
    EQ(USB_Device_CompleteControlStatus(s_token), scenario == 6U ? SYS_ERR_BUSY : SYS_OK);
    EQ(USB_Device_GetControlStatus(s_token, &status), SYS_OK);
    if (scenario == 6U)
    {
        EQ(status, USB_CONTROL_FAILED);
        EQ(s_endpoints[1][0].busy, false);
        EQ(USB_Device_CompleteControlStatus(s_token), SYS_ERR_INVALID_STATE);
        return 0;
    }
    EQ(status, USB_CONTROL_SUBMITTED);
    EQ(USB_Device_CompleteControlStatus(s_token), SYS_ERR_INVALID_STATE);
    if (scenario == 14U)
    {
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);
        EQ(USB_Device_GetControlStatus(s_token, &status), SYS_OK);
        EQ(status, USB_CONTROL_CANCELLED);
        return 0;
    }
    HAL_PCD_DataInStageCallback(&s_pcd, 0U);
    EQ(USB_Device_GetControlStatus(s_token, &status), SYS_OK);
    EQ(status, USB_CONTROL_COMPLETE);
    EQ(s_worker_owns_buffer, true);
    if (scenario == 16U)
    {
        EQ(USB_Device_WasControlStatusComplete(s_token), true);
        Request(0x80U, USB_REQ_GET_DESCRIPTOR, 0x0100U, 0U, 18U);
        EQ(USB_Device_GetControlStatus(s_token, &status), SYS_OK);
        EQ(status, USB_CONTROL_CANCELLED);
        EQ(USB_Device_WasControlStatusComplete(s_token), true);
        EQ(USB_Device_CompleteControlStatus(s_token), SYS_ERR_INVALID_STATE);
    }
    return 0;
}
HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *device, uint8_t endpoint)
{
    (void)device;
    (void)endpoint;
    return HAL_OK;
}

//执行器将IPSR设为异常模式，验证任务专用接口拒绝ISR调用
int DeferredTest_IsrComplete(void)
{
    return USB_Device_CompleteControlStatus(s_token);
}
int DeferredTest_IsrFail(void)
{
    return USB_Device_FailControlStatus(s_token);
}
