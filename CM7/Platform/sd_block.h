#ifndef CM7_SD_BLOCK_H
#define CM7_SD_BLOCK_H

#include "bsp_sdmmc.h"

//块设备访问者，任务句柄只保存在Platform内部
typedef enum
{
    SD_BLOCK_CLIENT_NONE = 0,
    SD_BLOCK_CLIENT_FATFS,
    SD_BLOCK_CLIENT_RAW
} SdBlockClient_t;

//当前本地卡状态，不把缓存ready当作仍然插卡的证明
typedef struct
{
    bool present;
    bool initialized;
    bool busy;
    bool fault;
} SdBlockStatus_t;

//当前任务取得独占访问权，文件系统与原始块访问不可并存
SysResult_t SD_Block_Claim(SdBlockClient_t client);

//当前任务归还空闲设备，未恢复的传输不能移交所有权
SysResult_t SD_Block_Release(SdBlockClient_t client);

//核对当前任务与客户端身份，不允许ISR冒用所有者
bool SD_Block_IsOwner(SdBlockClient_t client);

//读取插卡和底层状态，不执行文件操作
SysResult_t SD_Block_GetStatus(SdBlockStatus_t *status);

//获取当前已识别卡的CID副本
SysResult_t SD_Block_GetIdentity(uint32_t cid[4]);

//核对同步写入已结束，没有额外写缓存需要搬运
SysResult_t SD_Block_Sync(void);

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
