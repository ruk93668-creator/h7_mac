#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H
#include "sys_result.h"

//创建唯一的静态存储任务，不在调度前访问文件系统
SysResult_t StorageTask_Create(void);
#endif
