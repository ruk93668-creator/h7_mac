//使用真实ARMCC5布局与真实Camera私有上下文生成尺寸证据
#include "../CameraModuleB1/behavior_test.c"
#include <stddef.h>
//编译期核对只读停止事实字段，避免工具链插入隐藏布局变化
typedef char R2_StopSequenceOffset[(offsetof(CameraCaptureStopFacts_t,attempt_sequence)==0U)?1:-1];
typedef char R2_StopResultOffset[(offsetof(CameraCaptureStopFacts_t,hardware_result)==4U)?1:-1];
typedef char R2_StopFlagsOffset[(offsetof(CameraCaptureStopFacts_t,flags)==8U)?1:-1];
volatile uint32_t g_layout[18];
int R2_LayoutRun(uint32_t unused)
{
    (void)unused;
    g_layout[0]=sizeof(s_camera);
    g_layout[1]=sizeof(AppCameraQualification_t);
    g_layout[2]=sizeof(AppQualificationEntry_t);
    g_layout[3]=sizeof(AppQualificationDecision_t);
    g_layout[4]=sizeof(AppLifecycleMessage_t);
    g_layout[5]=sizeof(AppMessage_t);
    g_layout[6]=sizeof(CameraFrame_t);
    g_layout[7]=sizeof(s_camera_stack);
    g_layout[8]=CAMERA_TASK_PRIORITY;
    g_layout[9]=sizeof(AppQualificationResourceOps_t);
    g_layout[10]=offsetof(AppCameraQualification_t,rx);
    g_layout[11]=sizeof(CameraCaptureStopFacts_t);
    g_layout[12]=offsetof(AppLifecycleMessage_t,identity);
    g_layout[13]=offsetof(AppLifecycleIdentity_t,session_id);
    g_layout[14]=offsetof(AppLifecycleIdentity_t,request_id);
    g_layout[15]=offsetof(AppLifecycleIdentity_t,consumer_id);
    g_layout[16]=offsetof(AppLifecycleIdentity_t,consumer_generation);
    g_layout[17]=sizeof(g_camera_task_debug);
    return 0;
}
