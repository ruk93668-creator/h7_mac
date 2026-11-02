#include "../../CM7/Application/app_camera.c"
#include "../../CM7/Application/video_config.c"

SysResult_t Ov5640_ValidateConfig(const CameraConfig_t *c) { uint32_t n; return CameraConfig_GetFrameSize(c,&n); }
SysResult_t CameraCapture_ValidateStreamConfig(const CameraConfig_t *c) { uint32_t n; return CameraConfig_GetFrameSize(c,&n); }

static unsigned config_calls, init_calls, start_calls, stream_calls, process_calls;
static unsigned mode;
static bool missing;
static SysResult_t config_result, init_result, start_result;
static const CameraConfig_t *sensor_config, *media_config;
static SysResult_t Configure(Ov5640Device_t *camera, const CameraConfig_t *config)
{
    (void)camera; config_calls++; sensor_config=config; return config_result;
}
static const Ov5640Ops_t ops = {NULL, Configure};
static Ov5640Device_t device;
Device_t *Device_Find(const char *name)
{
    (void)name; return missing ? NULL : &device.base;
}
SysResult_t CameraCapture_Init(const CameraConfig_t *config)
{
    init_calls++; media_config=config; return init_result;
}
SysResult_t CameraCapture_Start(void)
{
    start_calls++;
    if (mode) return SYS_ERR_BUSY;
    if (start_result != SYS_OK) return start_result;
    mode=1; return SYS_OK;
}
SysResult_t CameraCapture_StartStream(void)
{
    stream_calls++;
    if (mode) return SYS_ERR_BUSY;
    if (start_result != SYS_OK) return start_result;
    mode=2; return SYS_OK;
}
void CameraCapture_Process(void) { process_calls++; }
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int App_TestRun(unsigned id)
{
    device.ops=&ops;
    switch(id)
    {
    case 0:
        CHECK(App_CameraInit()==SYS_OK);
        CHECK(config_calls==1 && init_calls==1 && mode==1);
        CHECK(sensor_config==media_config && media_config==App_CameraGetConfig());
        break;
    case 1:
        CHECK(App_CameraInitStream()==SYS_OK);
        CHECK(App_CameraInitStream()==SYS_ERR_BUSY);
        CHECK(config_calls==1 && init_calls==1 && mode==2);
        break;
    case 2:
        CHECK(App_CameraInit()==SYS_OK);
        CHECK(App_CameraInit()==SYS_ERR_BUSY);
        CHECK(App_CameraInitStream()==SYS_ERR_BUSY);
        CHECK(config_calls==1 && init_calls==1 && mode==1);
        break;
    case 3:
        CHECK(App_CameraInitStream()==SYS_OK);
        CHECK(App_CameraInit()==SYS_ERR_BUSY);
        CHECK(mode==2 && config_calls==1 && init_calls==1);
        break;
    case 4:
        missing=true;
        CHECK(App_CameraInit()==SYS_ERR_NOT_FOUND);
        CHECK(!init_calls && !start_calls && !stream_calls);
        missing=false; CHECK(App_CameraInitStream()==SYS_OK);
        break;
    case 5:
        device.ops=NULL; CHECK(App_CameraInit()==SYS_ERR_NOT_SUPPORTED);
        CHECK(!config_calls && !init_calls && !start_calls);
        break;
    case 6:
        config_result=SYS_ERR_IO; CHECK(App_CameraInit()==SYS_ERR_IO);
        CHECK(!init_calls && !start_calls && !s_camera_initialized);
        config_result=SYS_OK; CHECK(App_CameraInitStream()==SYS_OK);
        CHECK(config_calls==2 && init_calls==1);
        break;
    case 7:
        init_result=SYS_ERR_INVALID_STATE; CHECK(App_CameraInit()==SYS_ERR_INVALID_STATE);
        CHECK(config_calls==1 && init_calls==1 && !start_calls);
        init_result=SYS_OK; CHECK(App_CameraInitStream()==SYS_OK);
        CHECK(config_calls==1 && init_calls==2 && stream_calls==1);
        break;
    case 8:
        start_result=SYS_ERR_IO; CHECK(App_CameraInit()==SYS_ERR_IO);
        CHECK(s_camera_initialized && config_calls==1 && init_calls==1);
        CHECK(App_CameraInit()==SYS_ERR_IO);
        CHECK(config_calls==1 && init_calls==1);
        break;
    case 9:
        CHECK(App_CameraInit()==SYS_OK);
        mode=0; /* simulate successful Media Stop / Release */
        CHECK(App_CameraInitStream()==SYS_OK);
        CHECK(config_calls==1 && init_calls==1 && mode==2);
        break;
    case 10:
        CHECK(App_CameraInitStream()==SYS_OK);
        mode=0; CHECK(App_CameraInit()==SYS_OK);
        CHECK(config_calls==1 && init_calls==1 && mode==1);
        break;
    case 11:
        App_CameraRun(); CHECK(process_calls==1);
        CHECK(!config_calls && !init_calls && !start_calls && !stream_calls);
        break;
    case 12:
        CHECK(VideoConfig_RequestUpdate(NULL)==SYS_ERR_INVALID_ARG);
        CHECK(VideoConfig_RequestUpdate(VideoConfig_GetDefault())==SYS_ERR_NOT_SUPPORTED);
        CHECK(VideoConfig_GetDefault()->output_fps==10);
        break;
    default: return -1;
    }
    return 0;
}
