#ifndef CM4_APP_OLED_H
#define CM4_APP_OLED_H

#include "sys_result.h"
#include <stdint.h>

//OLED应用本地诊断，不进入共享协议
typedef struct
{
    uint32_t submitted_frames;//成功启动的整屏刷新次数
    uint32_t completed_frames;//四页全部发送成功的次数
    uint32_t busy_skips;//刷新未完成或IIC忙时跳过的次数
    uint32_t io_errors;//刷新提交或传输真实错误次数，不含BUSY
    uint32_t render_errors;//绘图失败次数
    uint32_t ipc_errors;//取得IPC本地视图失败次数
    uint32_t peer_state;//最近一次取样的对端通信状态
    uint32_t valid_mask;//最近一次取样的来源有效位
    uint32_t fresh_mask;//最近一次取样的来源新鲜位
    SysResult_t last_result;//最近一次应用操作结果
    SysResult_t last_error;//最近一次真实错误，成功刷新不自动清除
    SysResult_t ipc_result;//最近一次IPC视图读取结果
} AppOledDebug_t;

//供Keil观察界面更新、错误和刷新进展
extern volatile AppOledDebug_t g_oled_monitor_debug;

//获取OLED并启动唯一100ms界面更新Timer
SysResult_t App_OledInit(void);

//持续轮询OLED，推进四页异步刷新
void App_OledRun(void);

#endif
