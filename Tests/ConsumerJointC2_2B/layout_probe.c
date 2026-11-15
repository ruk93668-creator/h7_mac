#include "app_camera_consumer.h"
#include "app_messages.h"
volatile uint32_t g_failure;
volatile uint32_t g_joint_offsets[]={sizeof(AppMessage_t),sizeof(AppLifecycleMessage_t),offsetof(AppMessage_t,source),offsetof(AppMessage_t,command_id),offsetof(AppMessage_t,stream_epoch),offsetof(AppMessage_t,lease_id),offsetof(AppMessage_t,result),offsetof(AppCameraConsumer_t,granted)};
int Joint_Layout(uint32_t id){(void)id;return g_joint_offsets[0]-52U;}
