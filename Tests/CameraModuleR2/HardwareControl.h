#ifndef R2_HARDWARE_CONTROL_H
#define R2_HARDWARE_CONTROL_H

#include "sys_result.h"
#include <stdint.h>

#define R2_HW_SYSTEM_START      1U
#define R2_HW_SYSTEM_STOP       2U
#define R2_HW_STORAGE_VIDEO     3U
#define R2_HW_STORAGE_SNAPSHOT  4U
#define R2_HW_STORAGE_STOP      5U
#define R2_HW_STORAGE_RECOVER   6U

//Watch仅写command和submit_seq，其余字段只用于观察
//processed_seq最后发布，dispatch_result只表示公共请求接口返回值
typedef struct
{
    uint32_t ready;//Control已在合法任务上下文处理过邮箱，不代表Camera已运行
    uint32_t command;//本次请求操作码
    uint32_t submit_seq;//最后写入此字段提交，每次严格加一，不允许回绕
    uint32_t processed_seq;//最后完成分类和投递尝试的序号
    uint32_t processed_command;//该序号冻结的操作码
    SysResult_t dispatch_result;//入队或拒绝结果，不是业务完成结果
} R2HardwareMailbox_t;

//仅存在于独立验收镜像，正式固件不链接此符号
extern volatile R2HardwareMailbox_t g_r2_hw;

//在Control原处理和发布之后检查一次Watch请求
SysResult_t R2_HardwareControlProcess(void);

#endif
