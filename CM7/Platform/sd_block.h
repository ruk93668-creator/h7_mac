#ifndef CM7_SD_BLOCK_H
#define CM7_SD_BLOCK_H

#include "bsp_sdmmc.h"

//识卡并切换正式速度，只允许可阻塞的普通任务调用
SysResult_t SD_Block_Init(void);

//读取任意合法数量的512B扇区，失败时不得使用整次请求的数据
SysResult_t SD_Block_Read(uint32_t lba, uint32_t count, void *buffer, uint32_t timeout_ms);

//写入扇区，调用者负责文件系统或授权原始区域，超时可能已部分写入
SysResult_t SD_Block_Write(uint32_t lba, uint32_t count, const void *buffer, uint32_t timeout_ms);

//获取已识别卡信息，不发送扇区访问命令
SysResult_t SD_Block_GetInfo(BspSdmmcCardInfo_t *info);

//停止并重置故障外设，成功后需要再次Init
SysResult_t SD_Block_Recover(void);

#endif
