#ifndef V2_USB_MODULE_H
#define V2_USB_MODULE_H
#include "../../Configuration/v2_composition.h"
#include "../../Lifecycle/managed_lifecycle.h"
#include "usb_consumer.h"
#include "usb_format.h"
#include "usb_frame.h"

#define VUSB_MODE_STARTED 1U
#define VUSB_CLASS_READY 2U
#define VUSB_HOST_CONFIGURED 4U
#define VUSB_CAN_SUBMIT 8U
#define VUSB_FRAME_OWNED 16U
#define VUSB_READERS_QUIET 32U
#define VUSB_BRIDGE_IDLE 64U
#define VUSB_MODE_STOPPED 128U

typedef struct
{
    uint32_t flags;
    SysResult_t result; /* 当前操作对应的底层结果；陈旧故障由底层诊断保留。 */
} VUSBObservation;
typedef struct
{
    int (*is_owner)(void *);
    /* OK表示已提交。BUSY/NO_RESOURCE/NOT_FOUND保证无副作用，可重试。
     * 其他失败可能留下部分初始化，由事实核验及排空负责清理。 */
    SysResult_t (*start)(void *, uint32_t mode, const VideoStreamConfig_t *);
    void (*mode_process)(void *);
    SysResult_t (*cancel)(void *);
    void (*uvc_process)(void *);
    /* stop_mode: OK=底层停止调用成功，仍须观察对应的停止事实。 */
    SysResult_t (*stop_mode)(void *);
    SysResult_t (*observe)(void *, VUSBObservation *);
} VUSBOps;
typedef struct
{
    uint32_t magic, busy, bound, mode, work, start_issued, drain, mode_stop_done;
    SysResult_t first_fault, active_fault, stop_error;
    LCContext lifecycle;
    CoreMediaFormat format;
    VideoStreamConfig_t native;
    VUSBConsumer consumer;
    VUSBFrame frame;
    VUSBObservation observation;
    const VUSBOps *ops;
    void *port;
} VUSB;
typedef struct
{
    uint32_t lifecycle, drain, frame_phase, consumer_phase;
    MRGrant grant;
    VUSBObservation native;
    SysResult_t operation_error, first_fault, active_fault, stop_error, camera_stop_result;
} VUSBStatus;
extern const struct V2LifecycleOps VUSB_LifecycleOps;
SysResult_t VUSB_Init(VUSB *, uint32_t, uint32_t, const VUSBOps *, void *, const VUSBConsumerOps *,
                      void *, const VUSBFrameOps *, void *);
SysResult_t VUSB_QueryCapabilities(VUSB *, const CoreMediaFormat *, const CoreFrameRate *,
                                   VideoStreamConfig_t *);
SysResult_t VUSB_BindCandidate(VUSB *, const V2Registry *, const V2CompositionCandidate *,
                               uint32_t mode, const CoreFrameRate *);
SysResult_t VUSB_Request(VUSB *, const LCRequest *, uint32_t now);
SysResult_t VUSB_RunRound(VUSB *, uint32_t now);
SysResult_t VUSB_QueryStatus(VUSB *, VUSBStatus *);
SysResult_t VUSB_QueryExitEvidence(VUSB *, const MRGrant *);
SysResult_t VUSB_ReadCompletion(VUSB *, const V2ExecId *, LCCompletion *);
SysResult_t VUSB_ConfirmCompletion(VUSB *, const LCCompletion *);
#endif
