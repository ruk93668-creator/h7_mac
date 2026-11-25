#ifndef CM7_V3_USB_SERVICE_H
#define CM7_V3_USB_SERVICE_H

#include "app_context.h"
#include "sys_result.h"


//UsbTask接受的硬件生命周期命令
typedef enum
{
    USB_SERVICE_START = 16,
    USB_SERVICE_STOP,
    USB_SERVICE_HARDWARE_QUERY,
    USB_SERVICE_RECOVER
} UsbServiceOperation_t;

//登记任务可提交硬件命令，SYS_OK只表示成功入队
SysResult_t UsbService_Request(uint32_t command_id, UsbServiceOperation_t operation, uint32_t argument);

//仅UsbTask调用。Request成功表示入队；Read成功后直到Release期间可读取只读View。
SysResult_t UsbService_RequestFrame(void);
//取得当前租约关联的只读帧和分发序号
SysResult_t UsbService_ReadFrame(CameraFrame_t *frame, uint32_t *frame_id);
//确认读取方退出后登记归还，队列满也保留身份
SysResult_t UsbService_ReleaseFrame(void);
//关闭当前申请入口并推进已有交接退出
SysResult_t UsbService_CancelFrameRequest(void);

//静态Registry使用的启动入口，只有所属任务可以准备或撤回资源
//由所属任务逐轮推进真实操作，返回建议等待毫秒数
uint32_t AppV3_UsbAdvance(void);
//从UsbTask上下文运行队列和硬件事件循环
void AppV3_UsbRun(void *argument);
//由UsbTask检查输出配置并准备邮箱
SysResult_t AppV3_UsbInit(const AppV3Context_t *context);
//撤回尚未发布的USB队列与注册资源
void AppV3_UsbDiscard(void);

#endif
