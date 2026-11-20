//仅供链接；任何被测路径调用这些边界均令测试失败。
 #include "stm32h7xx_hal.h"
#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"
extern volatile uint32_t g_bulk_unexpected;
uint32_t g_allow_terminal;
void HAL_Delay(uint32_t Delay) { ++g_bulk_unexpected; }
void  HAL_GPIO_DeInit(GPIO_TypeDef  *GPIOx, uint32_t GPIO_Pin) { ++g_bulk_unexpected; }
void  HAL_GPIO_Init(GPIO_TypeDef  *GPIOx, const GPIO_InitTypeDef *GPIO_Init) { ++g_bulk_unexpected; }
void HAL_NVIC_ClearPendingIRQ(IRQn_Type IRQn) { if (!g_allow_terminal) ++g_bulk_unexpected; }
void HAL_NVIC_SetPriority(IRQn_Type IRQn, uint32_t PreemptPriority, uint32_t SubPriority) { ++g_bulk_unexpected; }
HAL_StatusTypeDef HAL_PCDEx_SetRxFiFo(PCD_HandleTypeDef *hpcd, uint16_t size) { if (g_allow_terminal) return HAL_OK; ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCDEx_SetTxFiFo(PCD_HandleTypeDef *hpcd, uint8_t fifo, uint16_t size) { if (g_allow_terminal) return HAL_OK; ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_DeInit(PCD_HandleTypeDef *hpcd) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *hpcd, uint8_t ep_addr) { if (g_allow_terminal) return HAL_OK; ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_EP_Close(PCD_HandleTypeDef *hpcd, uint8_t ep_addr) { if (g_allow_terminal) return HAL_OK; ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_EP_ClrStall(PCD_HandleTypeDef *hpcd, uint8_t ep_addr) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_EP_Flush(PCD_HandleTypeDef *hpcd, uint8_t ep_addr) { if (g_allow_terminal) return HAL_OK; ++g_bulk_unexpected; return HAL_ERROR; }
uint32_t          HAL_PCD_EP_GetRxCount(PCD_HandleTypeDef const *hpcd, uint8_t ep_addr) { ++g_bulk_unexpected; return 1; }
HAL_StatusTypeDef HAL_PCD_EP_Open(PCD_HandleTypeDef *hpcd, uint8_t ep_addr, uint16_t ep_mps, uint8_t ep_type) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_EP_Receive(PCD_HandleTypeDef *hpcd, uint8_t ep_addr, uint8_t *pBuf, uint32_t len) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_EP_SetStall(PCD_HandleTypeDef *hpcd, uint8_t ep_addr) { ++g_bulk_unexpected; return HAL_ERROR; }
void HAL_PCD_IRQHandler(PCD_HandleTypeDef *hpcd) { ++g_bulk_unexpected; }
HAL_StatusTypeDef HAL_PCD_Init(PCD_HandleTypeDef *hpcd) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_SetAddress(PCD_HandleTypeDef *hpcd, uint8_t address) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_Start(PCD_HandleTypeDef *hpcd) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PCD_Stop(PCD_HandleTypeDef *hpcd) { if (g_allow_terminal) return HAL_OK; ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_PWREx_DisableUSBReg (void) { ++g_bulk_unexpected; return HAL_ERROR; }
void HAL_PWREx_EnableUSBVoltageDetector   (void) { ++g_bulk_unexpected; }
void     HAL_RCCEx_CRSConfig(const RCC_CRSInitTypeDef *pInit) { ++g_bulk_unexpected; }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef  *PeriphClkInit) { ++g_bulk_unexpected; return HAL_ERROR; }
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *RCC_OscInitStruct) { ++g_bulk_unexpected; return HAL_ERROR; }
void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req) { ++g_bulk_unexpected; }
USBD_StatusTypeDef USBD_CtlSendStatus(USBD_HandleTypeDef *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *pdev, USBD_DescriptorsTypeDef *pdesc, uint8_t id) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_DataOutStage(USBD_HandleTypeDef *pdev, uint8_t epnum, uint8_t *pdata) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_DevConnected(USBD_HandleTypeDef *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_DevDisconnected(USBD_HandleTypeDef *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_IsoOUTIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_Reset(USBD_HandleTypeDef *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_Resume(USBD_HandleTypeDef *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_SOF(USBD_HandleTypeDef  *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_SetSpeed(USBD_HandleTypeDef *pdev, USBD_SpeedTypeDef speed) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_SetupStage(USBD_HandleTypeDef *pdev, uint8_t *psetup) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_LL_Suspend(USBD_HandleTypeDef *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
void USBD_ParseSetupRequest(USBD_SetupReqTypedef *req, uint8_t *pdata) { ++g_bulk_unexpected; }
USBD_StatusTypeDef USBD_RegisterClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass) { ++g_bulk_unexpected; return USBD_FAIL; }
USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *pdev) { ++g_bulk_unexpected; return USBD_FAIL; }
