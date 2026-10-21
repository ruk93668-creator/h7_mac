#include "framework_init.h"
#include <stddef.h>

//注册表Base & Limit
extern const FrameworkInitEntry_t Image$$ER_FW_INIT$$Base[];
extern const FrameworkInitEntry_t Image$$ER_FW_INIT$$Limit[];

//遍历并初始化同LEVEL的设备
static SysResult_t Framework_InitLevel(InitLevel_t level)
{
	const FrameworkInitEntry_t *entry;
	const FrameworkInitEntry_t *end;
	SysResult_t error = SYS_OK;
	
	if (level >= INIT_LEVEL_COUNT)
    {
        return SYS_ERR_INVALID_ARG;
    }	
	
    entry = Image$$ER_FW_INIT$$Base;
    end   = Image$$ER_FW_INIT$$Limit;
	
    while (entry < end)
    {
        if ((entry->level == level) &&
            (entry->init != NULL))
        {
            error = entry->init();
            if (error != SYS_OK)
                return error;
        }

        entry++;
    }

    return error;	
}

//初始化全部LEVEL的设备
SysResult_t Framework_InitRuntime(void)
{
    SysResult_t error = SYS_OK;

    error = Framework_InitLevel(INIT_LEVEL_PLATFORM);
    if (error != SYS_OK)
        return error;
    error = Framework_InitLevel(INIT_LEVEL_BUS);
    if (error != SYS_OK)
        return error;
    error = Framework_InitLevel(INIT_LEVEL_DEVICE);
    if (error != SYS_OK)
        return error;
    error = Framework_InitLevel(INIT_LEVEL_MEDIA);
    if (error != SYS_OK)
        return error;
    error = Framework_InitLevel(INIT_LEVEL_SERVICE);
    if (error != SYS_OK)
        return error;
    error = Framework_InitLevel(INIT_LEVEL_APPLICATION);
    if (error != SYS_OK)
        return error;

    return error;
}
