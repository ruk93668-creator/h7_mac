#ifndef CM7_V3_BOARD_SMOKE_H
#define CM7_V3_BOARD_SMOKE_H

#define APP_V3_BOARD_SMOKE_OFF 0
#define APP_V3_BOARD_SMOKE_CAMERA 1
#define APP_V3_BOARD_SMOKE_USB 2
#define APP_V3_BOARD_SMOKE_STORAGE 3

//正式构建关闭；实板测试只允许选择一个服务
#ifndef APP_V3_BOARD_SMOKE
#define APP_V3_BOARD_SMOKE APP_V3_BOARD_SMOKE_OFF
#endif

#if APP_V3_BOARD_SMOKE < APP_V3_BOARD_SMOKE_OFF || APP_V3_BOARD_SMOKE > APP_V3_BOARD_SMOKE_STORAGE
#error Invalid APP_V3_BOARD_SMOKE selection
#endif

#if APP_V3_BOARD_SMOKE != APP_V3_BOARD_SMOKE_OFF
#include "../Runtime/service_events.h"

#define APP_V3_BOARD_SMOKE_STEPS 4U
#define APP_V3_BOARD_SMOKE_FIRST_ID 0x7B530001UL
#define APP_V3_BOARD_SMOKE_TIMEOUT_MS 10000U

typedef enum
{
    APP_V3_SMOKE_IDLE = 0,
    APP_V3_SMOKE_SUBMIT,
    APP_V3_SMOKE_WAIT,
    APP_V3_SMOKE_COMPLETE,
    APP_V3_SMOKE_FAILED,
    APP_V3_SMOKE_TIMED_OUT
} AppV3BoardSmokeState_t;

typedef struct
{
    AppV3BoardSmokeState_t state;
    RuntimeEventTarget_t service;
    uint32_t step;
    uint32_t received;
    SysResult_t result;
    AppV3ServiceEvent_t events[APP_V3_BOARD_SMOKE_STEPS];
} AppV3BoardSmokeReport_t;

//只有Control写入；Keil Watch在暂停后读取完整快照
extern volatile AppV3BoardSmokeReport_t g_app_v3_board_smoke;
#endif

#endif
