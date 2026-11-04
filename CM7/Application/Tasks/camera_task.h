#ifndef CM7_CAMERA_TASK_H
#define CM7_CAMERA_TASK_H

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

    //当前采集代次内的完整帧和丢帧计数
    uint32_t captured_frames;
    uint32_t dropped_frames;

    //设备侧完整采集帧率，单位千分之一FPS，非配置目标值
    uint32_t measured_fps_milli;
} CameraTaskDebug_t;

//提供Camera任务的调试状态
extern volatile CameraTaskDebug_t g_camera_task_debug;

//创建并登记唯一的静态Camera任务
SysResult_t CameraTask_Create(void);

#endif
