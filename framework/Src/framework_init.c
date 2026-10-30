#include "framework_init.h"
#include <stddef.h>

//初始化表由移植层提供，不依赖链接器段符号
extern const FrameworkInitEntry_t g_framework_init_table[];
extern const uint32_t g_framework_init_count;

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
	
    entry = g_framework_init_table;
    end   = g_framework_init_table + g_framework_init_count;
	
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
