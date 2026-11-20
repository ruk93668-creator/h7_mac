#include "../../Core/app_context.h"
#include <stddef.h>

//StorageTask 保存已接受的策略，尚未创建文件或持有 SD 资源。
static const AppStorageConfig_t *s_storage_config;

SysResult_t AppV3_StorageInit(const AppV3Context_t *context)
{
    if (s_storage_config != NULL)
        return SYS_ERR_ALREADY_EXISTS;
    if (context->storage == NULL)
        return SYS_ERR_INVALID_STATE;

    //检查取样分母，不访问 SD 或挂载文件系统。
    if (context->storage->record_fps == 0U)
        return SYS_ERR_INVALID_ARG;

    s_storage_config = context->storage;
    return SYS_OK;
}
