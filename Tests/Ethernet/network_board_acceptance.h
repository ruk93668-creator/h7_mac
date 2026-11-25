#ifndef NETWORK_BOARD_ACCEPTANCE_H
#define NETWORK_BOARD_ACCEPTANCE_H

#include "sys_result.h"
#include <stdint.h>

typedef enum
{
    NETWORK_ACCEPTANCE_IDLE,
    NETWORK_ACCEPTANCE_STARTING,
    NETWORK_ACCEPTANCE_RUNNING,
    NETWORK_ACCEPTANCE_STOPPING,
    NETWORK_ACCEPTANCE_DONE,
    NETWORK_ACCEPTANCE_FAULT
} NetworkBoardAcceptancePhase_t;

typedef struct
{
    NetworkBoardAcceptancePhase_t phase;//命令序列进度，不代替生产服务状态
    uint32_t completed;//已取得完整停止终结的轮数
    uint32_t elapsed_ms;//当前活动阶段持续时间
    uint32_t active;//监督器确认的活动组件
    uint32_t uncertain;//没有退休证据的组件，不允许强行清零
    SysResult_t cleanup_result;//失败后的显式清理请求提交结果
    SysResult_t first_fault;//首个失败，验收不放宽标准
} NetworkBoardAcceptanceStatus_t;

//仅验收固件存在的值类型输入。1=100轮，2=联合长稳，3=有序停止。
//联合长稳必须先由用户指定非零 file_number，避免隐式开始 SD 写入。
extern volatile uint32_t g_n5_command, g_n5_cycles, g_n5_dwell_ms;
extern volatile uint32_t g_n5_joint_ms, g_n5_file_number;
extern volatile NetworkBoardAcceptanceStatus_t g_n5_acceptance;

//只能由 ControlTask 每轮调用；不访问 lwIP、CameraCapture 或 DMA。
void NetworkBoardAcceptance_Poll(void);

#endif

