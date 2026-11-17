#ifndef CM7_USB_MSC_STORAGE_H
#define CM7_USB_MSC_STORAGE_H
#include "usb_msc_backend.h"

//Worker下方设备接口只在StorageTask执行；quiescent必须核对真实硬件
//execute返回不代表缓冲可归还，直到quiescent确认为止
typedef struct
{
    SysResult_t (*init)(void);
    void (*media)(MscScsiMedia_t *media, uint32_t *generation);
    void (*execute)(const MscBlockRequest_t *request, MscBlockCompletion_t *completion);
    bool (*quiescent)(void);
} MscStorageDeviceOps_t;

SysResult_t USB_MSC_StorageInit(const MscStorageDeviceOps_t *device);
void USB_MSC_StorageProcess(void);
const MscBackendOps_t *USB_MSC_StorageBackend(void);
#endif
