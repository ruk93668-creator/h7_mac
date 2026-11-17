#ifndef CM7_USB_MSC_SD_H
#define CM7_USB_MSC_SD_H
#include "usb_msc_storage.h"
//仅供StorageTask启动；失败保持RAW租约，不自动挂载或归还MCU
const MscStorageDeviceOps_t *USB_MSC_SdDevice(void);
#endif
