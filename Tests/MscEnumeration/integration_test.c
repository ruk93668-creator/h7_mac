#include "../../CM7/Service/usb_test_device.c"
#include "hal_fixture.c"
#include "../../CM7/Service/usb_mode.c"
#include "../../CM7/Framework/framework_init.c"
uint32_t Platform_TimeGetMs(void) { return 0; }
#include "usb_msc_class.h"
volatile uint32_t g_msc_observation[6];
int MSC1_Run(void)
{
    __HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48); RCC->CR |= RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(),SYS_OK);
    EQ(USB_Mode_SelectBootMode(USB_MODE_TEST,NULL),SYS_OK);
    EQ(USB_Mode_GetMode(),USB_MODE_MSC);
    EQ(USB_Mode_Init(),SYS_OK); EQ(USB_Mode_Start(),SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);
    Request(0x80,USB_REQ_GET_DESCRIPTOR,0x200,0,255);
    g_msc_observation[0]=tx_size; g_msc_observation[1]=s_last_result; g_msc_observation[2]=s_stop_required; g_msc_observation[3]=s_configuration_length; g_msc_observation[4]=s_device.ep0_state; g_msc_observation[5]=stall_count;
    EQ(tx_size,32);EQ(tx_data[4],1);EQ(tx_data[13],2);EQ(tx_data[14],8);EQ(tx_data[15],6);EQ(tx_data[16],0x50);
    EQ(tx_data[20],0x82);EQ(tx_data[27],2);EQ(tx_data[22],64);EQ(tx_data[29],64);
    Configured();USB_Mode_Process();EQ(USB_Device_IsConfigured(),true);
    EQ(s_endpoints[1][2].opened,true);EQ(s_endpoints[0][2].opened,true);
    EQ(s_endpoints[1][2].type,USBD_EP_TYPE_BULK);EQ(s_endpoints[0][2].busy,false);
    EQ(s_endpoints[1][1].opened,false);EQ(s_tx_fifo_words[2],16);
    Request(0xa1,0xfe,0,0,1);EQ(stall_count>0,1);
    HAL_PCD_ResetCallback(&s_pcd);EQ(s_endpoints[1][2].opened,false);EQ(s_endpoints[0][2].opened,false);
    Configured();EQ(USB_Device_IsConfigured(),true);EQ(s_endpoints[1][2].opened,true);
    EQ(USB_Mode_Stop(),SYS_OK);EQ(s_endpoints[1][2].opened,false);EQ(s_endpoints[0][2].opened,false);
    return 0;
}

HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *p,uint8_t ep) { (void)p; (void)ep; return HAL_OK; }
