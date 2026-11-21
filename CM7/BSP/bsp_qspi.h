#ifndef CM7_BSP_QSPI_H
#define CM7_BSP_QSPI_H

#include "sys_result.h"
#include <stdint.h>

#define BSP_QSPI_EXPECTED_ID 0xEF4018UL

typedef enum
{
    BSP_QSPI_STAGE_IDLE = 0,
    BSP_QSPI_STAGE_PREPARE,
    BSP_QSPI_STAGE_HAL,
    BSP_QSPI_STAGE_COMMAND,
    BSP_QSPI_STAGE_RECEIVE,
    BSP_QSPI_STAGE_VERIFY,
    BSP_QSPI_STAGE_READY
} BspQspiStage_t;

//所有字段为快照；HAL READY 和理论时钟均不代表实板通信已验证。
typedef struct
{
    uint32_t stage;
    uint32_t ready;
    uint32_t fault;
    uint32_t attempts;
    uint32_t msp_called;
    SysResult_t prepare_result;
    SysResult_t msp_result;
    SysResult_t result;
    SysResult_t cleanup_result;
    uint32_t hal_status;
    uint32_t hal_error;
    uint32_t hal_state;
    uint32_t kernel_clock_hz;
    uint32_t serial_clock_hz;
    uint32_t prescaler;
    uint32_t init_timeout_ms;
    uint32_t elapsed_ms;
    uint32_t cleanup_elapsed_ms;
    uint32_t cr;
    uint32_t dcr;
    uint32_t sr;
    uint32_t jedec_id;
    uint32_t received_bytes;
} BspQspiDiagnostics_t;

//只允许 HAL Tick 可运行的普通线程；不注册 Initcall，不创建任务，不写 Flash。
SysResult_t BSP_QSPI_Init(void);

//释放本模块资源；故障锁存不因 DeInit 清除，本阶段需系统复位后重新识别。
SysResult_t BSP_QSPI_DeInit(void);

//仅返回已验证的缓存 ID；未 READY 时不修改输出，不再次发送命令。
SysResult_t BSP_QSPI_GetId(uint32_t *jedec_id);

//复制最近一次诊断；操作占用时返回 BUSY，避免读取撕裂状态。
SysResult_t BSP_QSPI_GetDiagnostics(BspQspiDiagnostics_t *diagnostics);

#endif
