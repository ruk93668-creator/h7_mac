#ifndef CM7_USB_UVC_H
#define CM7_USB_UVC_H

#include "sys_result.h"
#include "video_config_types.h"
#include <stdbool.h>
#include <stdint.h>

//发送状态；只通过API读取，不由上层修改
//WAIT_FRAME表示没有借用帧，但不保证Commit或Alt1已经就绪
typedef enum
{
    USB_UVC_WAIT_FRAME = 0,
    USB_UVC_WAIT_SLOT,
    USB_UVC_IN_FLIGHT,
    USB_UVC_WAIT_CONFIRM,
    USB_UVC_CANCELLING,
    USB_UVC_RECOVERY
} UsbUvcStreamState_t;

//一次一致性快照，不持有内部Class对象指针
typedef struct
{
    UsbUvcStreamState_t state;//当前发送或恢复状态
    SysResult_t result;//最近调度错误，与本次查询返回值分开
    uint32_t frame_offset;//已确认的图像字节，不含UVC头
    uint32_t token;//当前包标识，不是外部图像Handle
    uint32_t completed_frames;//当前Class生命周期成功整帧数
    bool frame_owned;//true时原图像仍被借用，不可改写或释放

    //Configured、Commit、Alt1及帧所有权均允许提交
    bool can_submit;
} UsbUvcStreamStatus_t;

//纯校验输出能力，不改变描述符或活动Class
SysResult_t USB_UVC_ValidateConfig(const VideoStreamConfig_t *config);

//注册Class前冷启动配置视频参数，配置后不可覆盖
SysResult_t USB_UVC_Configure(const VideoStreamConfig_t *config);

//启动线程中调用：Core Init之后、USB Start之前注册唯一Class
//返回Core注册结果；不初始化摄像头、不启动USB或视频发送
SysResult_t USB_UVC_Register(void);

//所属UsbTask调用：借用与已接受配置一致的YUY2整帧，length必须匹配帧长
//SYS_OK只代表接受借用，不代表USB完成；保持图像有效且不可改写
//BUSY表示已有帧；INVALID_STATE表示未就绪；INVALID_ARG表示参数非法
//失败不接受新帧；成功后的归还以有效状态查询frame_owned=false为准
SysResult_t USB_UVC_SubmitFrame(const uint8_t *frame, uint32_t length);

//所属UsbTask调用：登记取消，SYS_OK不代表Abort或归还已经完成
//继续Process并查询所有权；Abort失败保留图像，禁止强制回收
SysResult_t USB_UVC_CancelFrame(void);

//所属UsbTask调用：status非空，SYS_OK才允许使用本次快照
//INVALID_STATE可能是Class不存在或IRQ调用，失败时输出不更新
//合法所属USB任务且输出指针有效时，INVALID_STATE表示没有活动Class
//当前Class仅在确认停止并归还帧后销毁；IRQ调用的同名错误不能作归还依据
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *status);

//所属UsbTask持续调用：推进构包、完成确认、取消和恢复
//仅真实完成推进偏移；IRQ中调用直接返回；不负责产生图像
void USB_UVC_Process(void);

//查询设备侧累计EOF完成数，不代表Windows显示帧数
uint32_t USB_UVC_GetCompletedFrameCount(void);

#endif
