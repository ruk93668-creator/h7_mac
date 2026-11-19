#include "app_camera_generation.h"
#include <stddef.h>

//分配非零代次，拒绝耗尽和输出别名以免回退计数
SysResult_t App_QualificationNextGeneration(uint32_t *counter, uint32_t *generation)
{
    if (counter == NULL || generation == NULL || counter == generation)
        return SYS_ERR_INVALID_ARG;
    if (*counter == UINT32_MAX)
        return SYS_ERR_NO_RESOURCE;
    *generation = ++*counter;
    return SYS_OK;
}
