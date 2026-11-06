#ifndef CM7_BSP_DCMI_H
#define CM7_BSP_DCMI_H

#include "sys_result.h"
#include <stdint.h>
#include <stdbool.h>

#define BSP_DCMI_EVENT_FRAME (1UL << 0)
#define BSP_DCMI_EVENT_ERROR (1UL << 1)

//事件回调只作唤醒，不能在其中执行停止或释放缓冲区
//回调可能来自IRQ或调用中的短临界区
#define BSP_DCMI_NOTIFY_ERROR (1UL << 0)
#define BSP_DCMI_NOTIFY_PROGRESS (1UL << 1)
typedef void (*BspDcmiEventCallback_t)(uint32_t events, void *context);

//绑定静态采集进展与异常通知接收端
SysResult_t BSP_DCMI_SetEventCallback(BspDcmiEventCallback_t callback, void *context);

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
//Camera任务启动JPEG单帧，容量为1到65535个Word，调用方保证DMA可访问
SysResult_t BSP_DCMI_StartJpegSnapshot(uint32_t *buffer, uint32_t capacity_words);

//Camera任务推进FIFO排空与DMA停止，BUSY表示未完成，其他错误须Stop
//成功表示硬件已停止并填写已写入字节数，包含JPEG尾部Word填充
SysResult_t BSP_DCMI_ProcessJpeg(uint32_t *received_bytes);

//任务中全局中断开启时调用，停止并恢复HAL状态，不清除已记录事件
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

//启动固定长度连续采集，DMA双目标与完整帧回调共同轮换缓冲区
//两个缓冲区须4字节对齐、互不重叠，容量和DMA可访问性由调用方保证
//callback不能为空，context允许为NULL
SysResult_t BSP_DCMI_StartContinuous(
    uint32_t *buffer_m0,
    uint32_t *buffer_m1,
    uint32_t word_count,
    BspDcmiFrameCallback_t callback,
    void *context
);

//完成一块DMA字节区后，申请下一块地址；回调只操作有界元数据
//目标地址替换由BSP完成，失败后所有候选区都保留到Stop成功
typedef SysResult_t (*BspDcmiByteBlockCallback_t)(
    BspDcmiTarget_t target, uint32_t completed_address,
    uint32_t *next_address, void *context);

//记录输入帧边界，不代表DMA已把该帧全部写入SDRAM
typedef void (*BspDcmiByteFrameCallback_t)(void *context);

//静态回调表必须存活到Stop成功，BSP不读取Media对象
typedef struct
{
    BspDcmiByteBlockCallback_t block_complete;
    BspDcmiByteFrameCallback_t frame;
} BspDcmiByteStreamOps_t;

//Camera任务启动JPEG连续字节流，两块区均16字节对齐且互不重叠
//每块word_count为4的倍数，36至65532；正常帧间不关闭CAPTURE
SysResult_t BSP_DCMI_StartJpegContinuous(
    uint32_t *buffer_m0, uint32_t *buffer_m1, uint32_t word_count,
    const BspDcmiByteStreamOps_t *ops, void *context);

//仅在完整帧回调内替换本次完成的非活动DMA目标，一帧成功一次
//新缓冲区需满足整帧容量、DMA访问和Cache属性要求；失败交给Stop恢复
SysResult_t BSP_DCMI_ReplaceTarget(
    BspDcmiTarget_t completed_target,
    uint32_t *new_buffer
);

#endif
