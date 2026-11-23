#ifndef CM7_BSP_QSPI_H
#define CM7_BSP_QSPI_H

#include "sys_result.h"
#include <stdint.h>

#define BSP_QSPI_EXPECTED_ID 0xEF4018UL
#define BSP_QSPI_CAPACITY_BYTES 0x01000000UL
#define BSP_QSPI_MAX_READ_TIMEOUT_MS 60000U
#define BSP_QSPI_PAGE_BYTES 256U
#define BSP_QSPI_SECTOR_BYTES 4096U
#define BSP_QSPI_MAX_WRITE_TIMEOUT_MS 60000U

typedef enum
{
    BSP_QSPI_READ_STANDARD = 0,
    BSP_QSPI_READ_FAST,
    BSP_QSPI_READ_QUAD_OUTPUT
} BspQspiReadMode_t;

typedef struct
{
    uint32_t sr1, sr2, sr3;
    uint32_t busy, wel, qe, sus;
} BspQspiStatus_t;

typedef enum
{
    BSP_QSPI_READ_STAGE_IDLE = 0,
    BSP_QSPI_READ_STAGE_CHECK,
    BSP_QSPI_READ_STAGE_COMMAND,
    BSP_QSPI_READ_STAGE_RECEIVE,
    BSP_QSPI_READ_STAGE_COPY,
    BSP_QSPI_READ_STAGE_COMPLETE
} BspQspiReadStage_t;

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
    //Q1.2 诊断追加于末尾，不改变 Q1.1 初始化诊断字段的含义。
    uint32_t read_mode, read_address, read_requested, read_completed;
    uint32_t read_stage, read_opcode, read_elapsed_ms;
    uint32_t read_hal_status, read_hal_error, read_hal_state;
    uint32_t read_cr, read_dcr, read_sr, status_valid_mask;
    SysResult_t read_result, read_cleanup_result;
    uint32_t read_cleanup_elapsed_ms;
    BspQspiStatus_t status;
} BspQspiDiagnostics_t;

typedef enum
{
    BSP_QSPI_WRITE_CHECK = 0,
    BSP_QSPI_WRITE_STATUS,
    BSP_QSPI_WRITE_PREFLIGHT,
    BSP_QSPI_WRITE_ENABLE,
    BSP_QSPI_WRITE_WEL,
    BSP_QSPI_WRITE_COMMAND,
    BSP_QSPI_WRITE_TRANSMIT,
    BSP_QSPI_WRITE_BUSY,
    BSP_QSPI_WRITE_VERIFY,
    BSP_QSPI_WRITE_COMPLETE
} BspQspiWriteStage_t;

typedef struct
{
    SysResult_t result, cleanup_result;
    BspQspiWriteStage_t stage;
    uint32_t opcode, wire_opcode, address, requested, completed, failed_address;
    uint32_t hal_status, hal_error, hal_state, cr, dcr, sr;
    uint32_t sr1, sr2, sr3, status_valid_mask;
    uint32_t elapsed_ms, cleanup_elapsed_ms;
    uint32_t fault, flash_completion_unknown;
} BspQspiWriteDiagnostics_t;

//只允许 HAL Tick 可运行的普通线程；不注册 Initcall，不创建任务，不写 Flash。
SysResult_t BSP_QSPI_Init(void);

//释放本模块资源；故障锁存不因 DeInit 清除，本阶段需系统复位后重新识别。
SysResult_t BSP_QSPI_DeInit(void);

//仅返回已验证的缓存 ID；未 READY 时不修改输出，不再次发送命令。
SysResult_t BSP_QSPI_GetId(uint32_t *jedec_id);

//复制最近一次诊断；操作占用时返回 BUSY，避免读取撕裂状态。
SysResult_t BSP_QSPI_GetDiagnostics(BspQspiDiagnostics_t *diagnostics);

//只读三组状态；失败时不修改 status，诊断 valid_mask 标识本次已取得的原始字节。
SysResult_t BSP_QSPI_ReadStatus(BspQspiStatus_t *status, uint32_t timeout_ms);

//timeout_ms 为整次读取期限（1..60000ms），包括状态检查和所有分段。
//合法参数后 completed 先置零；失败仅 buffer[0..*completed) 已确认有效，后部保持原值。
//buffer/completed 必须为调用者独占、有效且不重叠的可写内存；不允许指向 BSP 私有资源。
//STANDARD 超过50MHz返回 NOT_SUPPORTED，不动态改频；Quad 仅为1-1-4输出。
SysResult_t BSP_QSPI_Read(BspQspiReadMode_t mode, uint32_t address,
    void *buffer, uint32_t size, uint32_t *completed, uint32_t timeout_ms);

//仅编程已擦除的完整请求。诊断中的 completed 是逐页验读通过的字节数。
//源缓冲区与诊断必须有效且互不重叠，操作期间由调用者独占。
SysResult_t BSP_QSPI_Program(uint32_t address, const void *buffer, uint32_t size,
    uint32_t timeout_ms, BspQspiWriteDiagnostics_t *diagnostics);

//仅擦除一个 4 KiB 对齐扇区。backup 必须是当前扇区的完整已保存副本。
//其有效性、独占和不重叠要求与 Program 一致，由调用者保证持久化备份。
SysResult_t BSP_QSPI_EraseSector(uint32_t address, const void *backup, uint32_t backup_size,
    uint32_t timeout_ms, BspQspiWriteDiagnostics_t *diagnostics);

#endif
