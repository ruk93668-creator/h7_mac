#ifndef CM7_USBD_CONF_H
#define CM7_USBD_CONF_H
#include "stm32h7xx.h"
#include <stdint.h>
#include <string.h>
#define USBD_MAX_NUM_INTERFACES 1U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 64U
#define USBD_SELF_POWERED 0U
#define USBD_LPM_ENABLED 0U
#define USBD_DEBUG_LEVEL 0U
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_malloc USB_Device_Alloc
#define USBD_free USB_Device_Free
#define USBD_Delay USBD_LL_Delay
#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)

//固定内存供后续Class使用，不使用malloc
void *USB_Device_Alloc(uint32_t size);

//归还固定Class内存
void USB_Device_Free(void *memory);
#endif
