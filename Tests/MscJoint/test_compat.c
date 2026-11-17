#include "../MscEnumeration/hal_fixture.c"
SysResult_t Usb_TestRegister(void);
HAL_StatusTypeDef HAL_PCD_EP_Abort(PCD_HandleTypeDef *device,uint8_t endpoint){(void)device;(void)endpoint;return HAL_OK;}
int Compat_Run(uint32_t scenario)
{
    (void)scenario;__HAL_RCC_USB_CONFIG(RCC_USBCLKSOURCE_HSI48);RCC->CR|=RCC_CR_HSI48RDY;
    EQ(USB_Device_Init(),SYS_OK);EQ(Usb_TestRegister(),SYS_OK);EQ(USB_Device_Start(),SYS_OK);
    HAL_PCD_ResetCallback(&s_pcd);Request(0x80,6,0x100,0,18);EQ(tx_size,18);
    Request(0x80,6,0x200,0,255);EQ(tx_size,18);EQ(tx_data[14],0xFF);
    Configured();EQ(USB_Device_IsConfigured(),true);Request(0x81,10,0,0,1);EQ(tx_size,1);EQ(tx_data[0],0);
    Request(0x81,0,0,0,2);EQ(tx_size,2);EQ(tx_data[0],0);EQ(tx_data[1],0);
    Request(0x21,0xFF,0,0,0);EQ(stall_count>0,true);EQ(s_endpoints[1][0].busy,false);
    Request(0x80,6,0x100,0,18);EQ(tx_size,18);EQ(s_endpoints[1][0].busy,true);
    Request(0x01,11,0,0,0);EQ(stall_count>0,true);
    HAL_PCD_ResetCallback(&s_pcd);EQ(USB_Device_IsConfigured(),false);
    Request(0x80,6,0x100,0,18);EQ(tx_size,18);Configured();EQ(USB_Device_IsConfigured(),true);
    EQ(USB_Device_Stop(),SYS_OK);return 0;
}
