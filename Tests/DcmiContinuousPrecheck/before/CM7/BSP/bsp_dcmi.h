#ifndef CM7_BSP_DCMI_H
#define CM7_BSP_DCMI_H

#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

#define BSP_DCMI_EVENT_FRAME (1UL << 0)
#define BSP_DCMI_EVENT_ERROR (1UL << 1)

//DMA双缓冲硬件目标
typedef enum
{
    BSP_DCMI_TARGET_M0 = 0,
    BSP_DCMI_TARGET_M1
} BspDcmiTarget_t;

//只传递结果，不管理帧缓冲所有权
typedef struct
{
	uint32_t flags;
	SysResult_t result;
	uint32_t dcmi_error;
	uint32_t dma_error;
} BspDcmiEvents_t;

//唤醒CM4前预配置引脚，避免GPIOB初始化冲突
void BSP_DCMI_EarlyInit(void);

//仅初始化DCMI和DMA，不启动采集
SysResult_t BSP_DCMI_Init(void);

//主循环调用，buffer须4字节对齐且DMA可访问，数量单位为32位字
//只支持1至65535字，调用方保证缓冲容量和MPU配置有效
//启动一次Snapshot采集，将图像写入指定缓冲区
SysResult_t BSP_DCMI_StartSnapshot(
	uint32_t *buffer,
	uint32_t word_count
);

//主循环且全局中断开启时调用，停止并恢复HAL状态，保留已有事件
SysResult_t BSP_DCMI_Stop(void);

//原子获取并清除事件，false表示无事件或空指针
//读取错误事件不代表硬件已恢复，错误后需成功Stop再启动
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *events);

//DMA双缓冲硬件目标
//BSP验证完整帧后通知上层
typedef SysResult_t (*BspDcmiFrameCallback_t)(
    BspDcmiTarget_t completed_target,
    void *context
);

//启动连续双目标DMA
SysResult_t BSP_DCMI_StartContinuous(
    uint32_t *buffer_m0,
    uint32_t *buffer_m1,
    uint32_t word_count,
    BspDcmiFrameCallback_t callback,
    void *context
);

//替换刚完成的非活动DMA目标
SysResult_t BSP_DCMI_ReplaceTarget(
    BspDcmiTarget_t completed_target,
    uint32_t *new_buffer
);

#endif
