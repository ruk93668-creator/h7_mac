// 协调 USB 生命周期、UVC 启停和真实退出证明。

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
// USB：绑定模块所有者及端口并初始化本地上下文。
SysResult_t VUSB_Init(VUSB *, uint32_t, uint32_t, const VUSBOps *, void *, const VUSBConsumerOps *,
                      void *, const VUSBFrameOps *, void *);
// USB：查询模块端口公开的格式及运行能力。
SysResult_t VUSB_QueryCapabilities(VUSB *, const CoreMediaFormat *, const CoreFrameRate *,
                                   VideoStreamConfig_t *);
// USB：校验并绑定已选配置候选，保留底层应用配置的独立结果。
SysResult_t VUSB_BindCandidate(VUSB *, const V2Registry *, const V2CompositionCandidate *,
                               uint32_t mode, const CoreFrameRate *);
// USB：登记带执行身份的生命周期请求，实际完成由模块轮次推进。
SysResult_t VUSB_Request(VUSB *, const LCRequest *, uint32_t now);
// 在 USB 所有者上下文推进生命周期、传输及归还。
SysResult_t VUSB_RunRound(VUSB *, uint32_t now);
// USB：读取模块的生命周期状态和观察到的业务事实。
SysResult_t VUSB_QueryStatus(VUSB *, VUSBStatus *);
// USB：分别读取退出所需的硬件、业务和媒体资源证据。
SysResult_t VUSB_QueryExitEvidence(VUSB *, const MRGrant *);
// USB：按身份读取模块已固定的完成结果。
SysResult_t VUSB_ReadCompletion(VUSB *, const V2ExecId *, LCCompletion *);
// USB：确认与请求身份匹配的完成已消费。
SysResult_t VUSB_ConfirmCompletion(VUSB *, const LCCompletion *);
#endif
