#ifndef CM7_USB_MSC_BACKEND_H
#define CM7_USB_MSC_BACKEND_H
#include "sys_result.h"
#include "usb_msc_scsi.h"

//三个身份独立携带，不能用端点序号替代介质代次或块请求Token
typedef struct
{
    uint32_t epoch;
    uint32_t token;
    uint32_t generation;
    MscScsiOperation_t operation;
    uint32_t lba;
    uint32_t blocks;
    uint8_t *buffer;
} MscBlockRequest_t;

typedef struct
{
    SysResult_t result;
    uint32_t completed_blocks;
} MscBlockCompletion_t;

typedef struct
{
    void (*media)(MscScsiMedia_t *status, uint32_t *generation);
    SysResult_t (*submit)(const MscBlockRequest_t *request);
    SysResult_t (*poll)(const MscBlockRequest_t *request, MscBlockCompletion_t *completion);
    void (*cancel)(uint32_t epoch);
    bool (*drained)(uint32_t epoch);
} MscBackendOps_t;

//仅供受控RAM测试固件，无文件系统、不接SD卡
const MscBackendOps_t *USB_MSC_RamBackend(void);
#endif
