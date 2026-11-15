#include "app_camera_consumer.h"
#include "app_messages.h"
// Compiler-measured wire/management layout; no firmware registration.
volatile const uint32_t g_layout[]={
 sizeof(AppCameraConsumer_t),sizeof(AppLifecycleTxContext_t),sizeof(AppLifecycleRxContext_t),sizeof(AppCameraQualification_t),sizeof(AppLifecycleMessage_t),sizeof(AppMessage_t),sizeof(CameraFrame_t),
 offsetof(AppCameraConsumer_t,request),offsetof(AppCameraConsumer_t,generation),offsetof(AppCameraConsumer_t,granted),
 offsetof(AppLifecycleTxContext_t,allocated_through),offsetof(AppLifecycleTxContext_t,issued_through),offsetof(AppLifecycleTxContext_t,pending),
 offsetof(AppLifecycleRxContext_t,replay),offsetof(AppLifecycleRxContext_t,reply_pending),offsetof(AppLifecycleRxContext_t,candidate),
 offsetof(AppCameraQualification_t,rx),offsetof(AppCameraQualification_t,session_id),
 offsetof(AppLifecycleMessage_t,identity),offsetof(AppLifecycleMessage_t,related_request_id),offsetof(AppLifecycleMessage_t,completion_flags),offsetof(AppLifecycleMessage_t,result),
 offsetof(AppMessage_t,source),offsetof(AppMessage_t,command_id),offsetof(AppMessage_t,stream_epoch),offsetof(AppMessage_t,lease_id),offsetof(AppMessage_t,result),offsetof(AppMessage_t,frame)};
int ID17_Layout(void){return g_layout[0]-232U;}
