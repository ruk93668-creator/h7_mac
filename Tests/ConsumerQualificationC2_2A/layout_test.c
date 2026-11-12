//ARMCC5实际编译原App_Camera和候选App_Camera，比较同一ABI下的私有尺寸
#include "../CameraModuleB1/behavior_test.c"
volatile uint32_t g_layout[12];
int C2A_LayoutRun(uint32_t id)
{
    (void)id;
    g_layout[0]=sizeof(s_camera);
    g_layout[1]=(uint32_t)&s_camera;
    g_layout[2]=sizeof(AppCameraQualification_t);
    g_layout[3]=sizeof(AppQualificationEntry_t);
    g_layout[4]=sizeof(AppQualificationDecision_t);
    g_layout[5]=sizeof(AppLifecycleMessage_t);
    g_layout[6]=sizeof(AppMessage_t);
    g_layout[7]=sizeof(CameraFrame_t);
    g_layout[8]=sizeof(s_camera_stack);
    g_layout[9]=CAMERA_TASK_PRIORITY;
    g_layout[10]=CAMERA_TASK_WAIT_MS;
    g_layout[11]=offsetof(AppCameraQualification_t,rx);
    return B1_TestRun(0U);
}
