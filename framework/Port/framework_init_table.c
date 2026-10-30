#include "framework_init.h"
#include <stddef.h>

//在占位项之前添加目标板初始化函数
const FrameworkInitEntry_t g_framework_init_table[] =
{
    {NULL, INIT_LEVEL_COUNT, NULL}
};

//排除最后的占位项，默认没有初始化任务
const uint32_t g_framework_init_count =
    sizeof(g_framework_init_table) / sizeof(g_framework_init_table[0]) - 1U;
