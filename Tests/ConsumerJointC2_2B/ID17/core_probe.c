#include "../../ConsumerIntegrationC2_2B/consumer_test.c"
#define ID17_QUAL_OFFSET 0U

// Read-only layout metadata; never writes Consumer, Qualification, or Replay state.
volatile const uint32_t g_id17_layout[]={
 sizeof(AppCameraConsumer_t),offsetof(AppCameraConsumer_t,phase),offsetof(AppCameraConsumer_t,result),
 offsetof(AppCameraConsumer_t,session),offsetof(AppCameraConsumer_t,generation),offsetof(AppCameraConsumer_t,granted),
 offsetof(AppCameraConsumer_t,request)+offsetof(AppLifecycleMessage_t,identity)+offsetof(AppLifecycleIdentity_t,request_id),
 offsetof(AppCameraQualification_t,session_id),offsetof(AppCameraQualification_t,rx),sizeof(AppLifecycleRxContext_t),
 offsetof(AppLifecycleRxContext_t,final_reply_enqueued),sizeof(AppQualificationEntry_t),offsetof(AppQualificationEntry_t,phase),
 ID17_QUAL_OFFSET,sizeof(AppCameraQualification_t)};
volatile uint32_t g_id17_layout_anchor;

// Invoke the unchanged original ID17; expected rejection does not rewrite its FAIL.
int ID17_Core(uint32_t id){int r=Integration_Run(id);g_id17_layout_anchor=g_id17_layout[0];return r;}
