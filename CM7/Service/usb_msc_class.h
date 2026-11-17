#ifndef CM7_USB_MSC_CLASS_H
#define CM7_USB_MSC_CLASS_H
#include "sys_result.h"
/* Enumeration only: no Bulk receive, BOT, SCSI, LUN or storage access. */
SysResult_t USB_MSC_Register(void);
#endif
