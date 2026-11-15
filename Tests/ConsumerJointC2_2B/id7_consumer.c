// Instrument only the call boundary; the included production Consumer is unchanged.
#include "app_camera_consumer.h"
SysResult_t ID7_Stage(AppLifecycleTxContext_t *tx,const AppLifecycleMessage_t *message);
#define App_LifecycleTxStage ID7_Stage
#include "../../CM7/Application/app_camera_consumer.c"
