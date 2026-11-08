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

#endif
