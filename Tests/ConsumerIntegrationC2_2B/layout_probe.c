#include "app_camera_consumer.h"
#include "app_messages.h"
#include "camera_task.h"
volatile uint32_t g_failure;
volatile uint32_t g_c2b_abi[]={sizeof(AppCameraConsumer_t),sizeof(AppLifecycleTxContext_t),
 sizeof(AppLifecycleRxContext_t),sizeof(AppCameraQualification_t),sizeof(AppLifecycleMessage_t),
 sizeof(AppMessage_t),sizeof(CameraFrame_t),sizeof(CameraTaskDebug_t),
 offsetof(AppCameraConsumer_t,request),offsetof(AppCameraConsumer_t,next_generation)};
int Layout_Run(uint32_t id){(void)id;return (int)(g_failure+g_c2b_abi[0]-232U);}
