/* Execute unmodified HAL/LL; this fixture supplies no replacement stop/flush routine. */
#include "stm32h7xx_hal.h"

static PCD_HandleTypeDef s_test_pcd;
volatile uint32_t g_endpoint_contract[11];

/* Link-only dependencies; every invocation is a test failure. */
void HAL_Delay(uint32_t delay) { (void)delay; ++g_endpoint_contract[10]; }
uint32_t HAL_RCC_GetHCLKFreq(void) { ++g_endpoint_contract[10]; return 0U; }
HAL_StatusTypeDef HAL_PCDEx_ActivateLPM(PCD_HandleTypeDef *pcd)
{ (void)pcd; ++g_endpoint_contract[10]; return HAL_ERROR; }
void HAL_PCDEx_LPM_Callback(PCD_HandleTypeDef *pcd, PCD_LPM_MsgTypeDef msg)
{ (void)pcd; (void)msg; ++g_endpoint_contract[10]; }

int EndpointContract_Run(uint32_t scenario)
{
    uint32_t USBx_BASE = (uint32_t)USB2_OTG_FS;
    HAL_StatusTypeDef actual = HAL_ERROR;
    HAL_StatusTypeDef expected = HAL_ERROR;
    uint8_t address = 0x82U;
    s_test_pcd.Instance = USB2_OTG_FS;
    s_test_pcd.Lock = HAL_UNLOCKED;
    s_test_pcd.IN_ep[2].num = 2U;
    s_test_pcd.IN_ep[2].is_in = 1U;
    s_test_pcd.OUT_ep[2].num = 2U;
    s_test_pcd.OUT_ep[2].is_in = 0U;
    s_test_pcd.IN_ep[2].type = EP_TYPE_BULK;
    s_test_pcd.IN_ep[2].maxpacket = 64U;
    s_test_pcd.OUT_ep[2].type = EP_TYPE_BULK;
    s_test_pcd.OUT_ep[2].maxpacket = 64U;
    USB2_OTG_FS->GAHBCFG = USB_OTG_GAHBCFG_GINT;

    if (scenario == 0U || scenario == 1U || scenario == 11U || scenario == 14U)
    {
        if (scenario == 1U) address = 0x02U;
        if (scenario == 11U) s_test_pcd.Lock = HAL_LOCKED;
        actual = HAL_PCD_EP_Flush(&s_test_pcd, address);
        expected = scenario == 11U ? HAL_BUSY : HAL_OK;
    }
    else if (scenario == 2U || scenario == 15U)
    {
        actual = USB_FlushTxFifo(USB2_OTG_FS, 2U);
        expected = scenario == 15U ? HAL_OK : HAL_TIMEOUT;
    }
    else if (scenario == 3U || scenario == 16U)
    {
        actual = USB_FlushRxFifo(USB2_OTG_FS);
        expected = scenario == 16U ? HAL_OK : HAL_TIMEOUT;
    }
    else if (scenario == 4U || scenario == 12U)
    {
        if (scenario == 12U) s_test_pcd.Lock = HAL_LOCKED;
        actual = HAL_PCD_Stop(&s_test_pcd);
        expected = scenario == 12U ? HAL_BUSY : HAL_OK;
    }
    else
    {
        address = (scenario == 6U || scenario == 8U || scenario == 10U) ? 0x02U : 0x82U;
        if (scenario == 13U) s_test_pcd.Lock = HAL_LOCKED;
        actual = HAL_PCD_EP_Abort(&s_test_pcd, address);
        expected = (scenario == 5U || scenario == 6U) ? HAL_ERROR : HAL_OK;
    }
    g_endpoint_contract[0] = scenario;
    g_endpoint_contract[1] = actual;
    g_endpoint_contract[2] = expected;
    g_endpoint_contract[3] = USB2_OTG_FS->GRSTCTL;
    g_endpoint_contract[4] = USBx_INEP(2U)->DIEPCTL;
    g_endpoint_contract[5] = USBx_OUTEP(2U)->DOEPCTL;
    g_endpoint_contract[6] = USBx_INEP(2U)->DIEPINT;
    g_endpoint_contract[7] = USBx_OUTEP(2U)->DOEPINT;
    g_endpoint_contract[8] = USB2_OTG_FS->GAHBCFG;
    g_endpoint_contract[9] = USBx_DEVICE->DCTL;
    return actual == expected ? 0 : 1;
}
