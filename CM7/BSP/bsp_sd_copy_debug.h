#ifndef BSP_SD_COPY_DEBUG_H
#define BSP_SD_COPY_DEBUG_H

#include <stdint.h>

//保存当前复制段，sequence为奇数时说明标记尚未准备完成
typedef struct
{
    uint32_t sequence;//标记更新序号
    uint32_t active;//1表示已进入复制观察区间
    uint32_t source;//本段源地址
    uint32_t destination;//本段目标地址
    uint32_t bytes;//本段复制字节数
    uint32_t dma_lisr_before;//复制前DMA1原始标志
} BspSdCopyLive_t;

//锁存首次DCMI错误，不在后续错误或新一轮录像时覆盖
typedef struct
{
    uint32_t state;//0未捕获，1正在捕获，2捕获完成
    uint32_t copy_consistent;//1表示复制标记完整，0时不可据此解释地址
    uint32_t copy_active;//1表示错误处理时仍在复制观察区间
    uint32_t sequence;//出错时复制标记序号
    uint32_t source;//复制源地址
    uint32_t destination;//复制目标地址
    uint32_t bytes;//复制长度，非活动时只是上一段信息
    uint32_t dma_lisr_before;//这段复制开始前的DMA标志
    uint32_t dma_lisr_fault;//捕获时DMA标志，不能替代复制前标志
    uint32_t dma_error;//软件识别出的DMA错误
    uint32_t dma_cr;//捕获时DMA控制配置
    uint32_t dma_fcr;//捕获时DMA FIFO配置及状态
    uint32_t dma_ndtr;//捕获时DMA剩余Word数
    uint32_t dcmi_risr;//捕获时DCMI原始中断标志
    uint32_t sd_idmactrl;//捕获时SDMMC IDMA使能状态
    uint32_t sd_idmabase0;//捕获时SDMMC IDMA地址
    int32_t result;//首次DCMI错误结果
} BspSdCopyFault_t;

//把实时标记和首次故障分开，后续复制不会覆盖故障证据
typedef struct
{
    BspSdCopyLive_t live;
    BspSdCopyFault_t fault;
} BspSdCopyDebug_t;

//仅供本轮Watch观察，复位后重新捕获，不用于业务判断
extern volatile BspSdCopyDebug_t g_sd_copy_debug;

//正式固件使用SD强实现，独立DCMI构建保留诊断空入口
void BSP_SDMMC_CaptureCopyFault(int32_t result, uint32_t dma_error);

#endif
