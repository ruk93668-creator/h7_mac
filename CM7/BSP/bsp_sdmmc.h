#ifndef CM7_BSP_SDMMC_H
#define CM7_BSP_SDMMC_H

#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

//卡座插卡时SW接通VDD，拆除外部上拉后使用内部下拉检测
#ifndef BSP_SDMMC_DETECT_ACTIVE_LOW
#define BSP_SDMMC_DETECT_ACTIVE_LOW 0
#endif

#if BSP_SDMMC_DETECT_ACTIVE_LOW != 0 && BSP_SDMMC_DETECT_ACTIVE_LOW != 1
#error BSP_SDMMC_DETECT_ACTIVE_LOW_must_be_zero_or_one
#endif

//诊断阶段只表示最近一次初始化停在哪里，不等于卡的协议状态
typedef enum
{
    BSP_SDMMC_STAGE_IDLE = 0,
    BSP_SDMMC_STAGE_DETECT,
    BSP_SDMMC_STAGE_CLOCK,
    BSP_SDMMC_STAGE_INIT,
    BSP_SDMMC_STAGE_INFO,
    BSP_SDMMC_STAGE_STATE,
    BSP_SDMMC_STAGE_READY
} BspSdmmcStage_t;

//卡信息为本地副本，不公开HAL句柄或数据缓冲区
typedef struct
{
    uint32_t card_type;//保留HAL类型编号，SDHC和SDXC共用一种类型
    uint32_t card_version;//卡协议版本编号
    uint32_t logical_block_count;//以逻辑块为单位的容量
    uint32_t logical_block_size;//每逻辑块字节数
    uint32_t physical_block_count;//HAL解析的物理块数量
    uint32_t physical_block_size;//HAL解析的物理块字节数
    uint64_t capacity_bytes;//64位容量，避免大卡乘法溢出
    uint32_t card_state;//CMD13返回的卡状态
} BspSdmmcCardInfo_t;

//失败清理前保存诊断，清理失败不会覆盖原始初始化错误
typedef struct
{
    uint32_t stage;//最近初始化阶段
    uint32_t detect_raw;//PH14原始高低电平
    uint32_t card_present;//按当前检测极性解释的插卡状态，需实板核对
    uint32_t detect_active_low;//当前编译采用的检测极性
    uint32_t kernel_clock_hz;//实际SDMMC内核时钟
    uint32_t sd_clock_hz;//CLKCR回读计算的SD输出时钟
    uint32_t clock_div;//实际分频值
    uint32_t bus_width;//实际总线宽度，当前必须为1
    uint32_t rcc_status;//时钟选择HAL返回值，未调用时为HAL_OK
    uint32_t status_register;//清理前SDMMC STA快照
    uint32_t clock_register;//清理前SDMMC CLKCR快照
    uint32_t response1;//清理前最近命令响应，可能已被后续命令更新
    uint32_t hal_status;//SD HAL调用返回值
    uint32_t hal_error;//失败清理前的HAL错误位
    uint32_t hal_state;//失败清理前的HAL句柄状态
    uint32_t init_attempts;//有效执行环境下的初始化尝试次数
    SysResult_t init_result;//最近一次初始化结果
    SysResult_t cleanup_result;//最近一次清理结果
    uint32_t elapsed_ms;//最近初始化和必要清理经过的时间
    uint32_t ready;//是否已取得完整且验证通过的卡信息
} BspSdmmcDiagnostics_t;

//配置检测输入并读取PH14，不能将未经实测的极性当成硬件验收
SysResult_t BSP_SDMMC_ReadDetect(uint32_t *raw_level, bool *present);

//低速1-bit识卡，只允许HAL Tick可运行的非中断上下文
SysResult_t BSP_SDMMC_Init(void);

//取得已识别卡的信息副本，不进行扇区读写
SysResult_t BSP_SDMMC_GetCardInfo(BspSdmmcCardInfo_t *info);

//取得完整本地诊断副本，不重新发卡命令
SysResult_t BSP_SDMMC_GetDiagnostics(BspSdmmcDiagnostics_t *diagnostics);

//释放本外设资源，失败时保留占用以便再次清理
SysResult_t BSP_SDMMC_DeInit(void);

//SDMMC专用AXI区域，32KiB一次最多64个逻辑扇区
#define BSP_SDMMC_DMA_BASE   0x24000000UL
#define BSP_SDMMC_DMA_BYTES  (32UL * 1024UL)
#define BSP_SDMMC_BLOCK_SIZE 512UL
#define BSP_SDMMC_DMA_BLOCKS (BSP_SDMMC_DMA_BYTES / BSP_SDMMC_BLOCK_SIZE)

//等待适配只使用普通C类型，不把RTOS句柄传入BSP
//wait允许虚假唤醒，BSP每次醒来重新检查硬件和统一截止时间
//notify只由SDMMC IRQ调用，context必须在驱动使用期间始终有效
typedef struct
{
    bool (*can_wait)(void *context);
    SysResult_t (*wait)(void *context, uint32_t timeout_ms);
    void (*notify)(void *context);
    void *context;
} BspSdmmcWaitOps_t;

//记录最近一次块请求和清理结果，不把HAL READY当作完成证明
typedef struct
{
    uint32_t active;//请求占用内部缓冲区，FAULT时可能继续保留
    uint32_t fault;//必须显式恢复后才能再请求
    uint32_t transfer_ready;//已经完成4-bit及速度模式配置
    uint32_t high_speed;//成功执行High Speed切换，不等同卡宣传速度
    uint32_t lba;//本次请求起始扇区
    uint32_t requested_blocks;//本次请求总扇区数
    uint32_t completed_blocks;//已经确认成功的批次扇区数
    uint32_t write;//本次是否写入
    uint32_t irq_count;//SDMMC中断次数
    uint32_t lost_irq_polls;//任务检测到事件但未收到IRQ通知的次数
    uint32_t status;//清理前STA原始值
    uint32_t hal_error;//原始HAL错误，不被Abort覆盖
    uint32_t elapsed_ms;//整次请求时间，含恢复
    uint32_t dma_address;//实际内部缓冲区地址
    uint32_t dma_bytes;//内部区域容量
    SysResult_t result;//原始操作结果
    SysResult_t recovery_result;//停止和恢复结果
} BspSdmmcIoDiagnostics_t;

//启动期配置专用MPU区域，不改变全局Cache开关
SysResult_t BSP_SDMMC_MemoryInit(void);

//验证链接地址和MPU属性，拒绝缓存属性或地址不匹配的缓冲区
bool BSP_SDMMC_MemoryValid(void);

//登记静态等待适配，只允许首次登记或相同适配重复登记
SysResult_t BSP_SDMMC_SetWaitOps(const BspSdmmcWaitOps_t *ops);

//低速识卡后切换4-bit，支持High Speed时使用40MHz，否则24MHz
SysResult_t BSP_SDMMC_EnableTransferMode(void);

//任务侧读取扇区，IDMA仅访问内部AXI区，成功后复制到调用者
SysResult_t BSP_SDMMC_ReadBlocks(uint32_t lba, uint32_t count, void *buffer, uint32_t timeout_ms);

//任务侧写入扇区，调用者数据先复制到内部AXI区，写超时不自动重试
SysResult_t BSP_SDMMC_WriteBlocks(uint32_t lba, uint32_t count, const void *buffer, uint32_t timeout_ms);

//获取最近一次块请求诊断，事务运行中返回BUSY避免读取半更新状态
SysResult_t BSP_SDMMC_GetIoDiagnostics(BspSdmmcIoDiagnostics_t *diagnostics);

//获取当前识卡会话的原始CID，仅用于卡身份校验
SysResult_t BSP_SDMMC_GetIdentity(uint32_t cid[4]);

//显式停止并重置SDMMC1，成功后仍需要重新识卡和配置速度
SysResult_t BSP_SDMMC_Recover(void);

#endif
