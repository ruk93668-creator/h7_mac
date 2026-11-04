#ifndef CM7_USB_TASK_H
#define CM7_USB_TASK_H

#include "sys_result.h"
#include <stdint.h>

typedef struct
{

    //是否已经收到启动屏障
    uint32_t boot_seen;

    //累计处理通知的次数
    uint32_t wake_count;

    //累计等待超时次数，永久等待的任务通常保持0
    uint32_t timeout_count;

    //历史最小剩余栈空间，单位为StackType_t
    uint32_t stack_free_words;

    //最近一次任务操作结果
    SysResult_t result;

    //任务自身的运行阶段
    uint32_t state;

    //最近一次接收的控制命令编号
    uint32_t command_id;

    //累计真实EOF完成帧数，不把Submit成功当作完成
    uint32_t completed_frames;

    //设备侧EOF完成速率，单位千分之一FPS
    uint32_t measured_fps_milli;

    //最近一次任务处理时看到的真实SOF序号
    uint32_t sof_sequence;

    //多次SOF合并为一次任务处理时跳过的机会数
    uint32_t coalesced_sof_slots;

    //两次任务处理之间最大的SOF序号增量
    uint32_t max_sof_gap;
} UsbTaskDebug_t;

//提供Usb任务的调试状态
extern volatile UsbTaskDebug_t g_usb_task_debug;

//创建并登记唯一的静态Usb任务
SysResult_t UsbTask_Create(void);

#endif
