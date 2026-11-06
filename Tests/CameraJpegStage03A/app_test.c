#define APP_CAMERA_JPEG_TEST_ENABLED 1
#define APP_VIDEO_TEST_MODE 1
#define Lifecycle_TestRun Historical_LifecycleRun
#include "../Ov5640Lifecycle/lifecycle_test.c"
#undef Lifecycle_TestRun
#include "../../CM7/Application/app_camera_jpeg.c"
static bool camera_owner=true;
bool App_MessagesIsOwner(RuntimeEventTarget_t owner){return camera_owner && owner==RUNTIME_TARGET_CAMERA;}
int Lifecycle_TestRun(uint32_t id)
{
 CHECK(Init()==0);
 if(id==0){camera_owner=false;App_CameraJpegProcess();CHECK(g_camera_jpeg_test.state==0);CHECK(sensor_starts==0);return 0;}
 if(id==1)board_result=SYS_ERR_IO;
 App_CameraJpegProcess();
 if(id==1){CHECK(g_camera_jpeg_test.state==3);CHECK(g_camera_jpeg_test.result==SYS_ERR_IO);CHECK(dcmi_starts==0);return 0;}
 CHECK(g_camera_jpeg_test.state==1);CHECK(s_capture.config.format==CAMERA_PIXEL_FORMAT_JPEG);CHECK(dcmi_starts==1);
 if(id==2){App_CameraJpegProcess();CHECK(dcmi_starts==1);CHECK(g_camera_jpeg_test.state==1);return 0;}
 if(id==7){tick+=1000;App_CameraJpegProcess();CHECK(g_camera_jpeg_test.state==3);CHECK(g_camera_jpeg_test.result==SYS_ERR_TIMEOUT);return 0;}
 //应用层测试单独注入Media READY；真实JPEG边界由Media专项验证
 s_capture.state=CAMERA_CAPTURE_READY;s_capture.snapshot.valid_bytes=52;
 App_CameraJpegProcess();CHECK(g_camera_jpeg_test.state==2);CHECK(g_camera_jpeg_test.frame_address==CAMERA_BUFFER_BASE);CHECK(g_camera_jpeg_test.size_bytes==52);
 if(id==3 || id==6){App_CameraJpegProcess();CHECK(g_camera_jpeg_test.state==2);CHECK(s_capture.state==CAMERA_CAPTURE_READY);CHECK(g_camera_jpeg_test.stop_attempted==0);return 0;}
 if(id==5){fail_reg=0x3008;fail_value=0x42;}
 g_camera_jpeg_test.release_request=1;App_CameraJpegProcess();
 CHECK(g_camera_jpeg_test.stop_attempted==1);
 CHECK(g_camera_jpeg_test.state==(id==5?3U:4U));
 CHECK(g_camera_jpeg_test.stop_result==(id==5?SYS_ERR_IO:SYS_OK));
 return 0;
}
