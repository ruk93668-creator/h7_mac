#ifndef CM7_APP_OV5640_H
#define CM7_APP_OV5640_H

#include "sys_result.h"

#define APP_OV5640_TEST_COUNT 10U

//单次芯片ID读取结果
typedef struct
{
    uint16_t chip_id;
    SysResult_t result;
} Ov5640ProbeSample_t;

//保存测试次数和每次读取结果
typedef struct
{
    uint32_t count;    //已执行次数
    uint32_t passed;   //成功次数
    uint32_t failed;   //失败次数
    uint32_t done;     //测试完成标记
    Ov5640ProbeSample_t samples[APP_OV5640_TEST_COUNT];
} Ov5640ProbeReport_t;

extern volatile Ov5640ProbeReport_t g_ov5640_probe;

//初始化摄像头应用
SysResult_t App_Ov5640Init(void);

//轮询到期任务并读取芯片ID
void App_Ov5640Run(void);

#endif
