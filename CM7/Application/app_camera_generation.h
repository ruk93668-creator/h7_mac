#ifndef CM7_APP_CAMERA_GENERATION_H
#define CM7_APP_CAMERA_GENERATION_H

#include "sys_result.h"
#include <stdint.h>

//给未来消费者的新尝试分配代次，失败和取消均不得回退计数
SysResult_t App_QualificationNextGeneration(uint32_t *counter, uint32_t *generation);

#endif
