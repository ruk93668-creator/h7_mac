#define Lifecycle_TestRun Historical_LifecycleRun
#include "../Ov5640Lifecycle/lifecycle_test.c"
#undef Lifecycle_TestRun
#include "fifo_dependency.inc"
int Lifecycle_TestRun(uint32_t id)
{
 CameraConfig_t cfg={160,120,CAMERA_PIXEL_FORMAT_JPEG};
 CHECK(Init()==0);
 if(id==0){CHECK(Ov5640_ValidateConfig(&cfg)==SYS_OK);return 0;}
 if(id==1){cfg.width=320;CHECK(Ov5640_ValidateConfig(&cfg)==SYS_ERR_NOT_SUPPORTED);return 0;}
 if(id==2){fail_reg=0x4300;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_IO);CHECK(!s_sensor_configured);CHECK(s_ov5640.base.state==DEVICE_STATE_READY);return 0;}
 if(id==3){regs[0x300b]=0;CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_PROTOCOL);CHECK(writes==0);return 0;}
 CHECK(Configure(CAMERA_PIXEL_FORMAT_JPEG)==0);
 CHECK(regs[0x4300]==0x30);CHECK(regs[0x501f]==0);CHECK((regs[0x3821]&0x20)!=0);CHECK((regs[0x3002]&0x1C)==0);CHECK((regs[0x3006]&0x28)==0x28);
 CHECK(regs[0x3008]==0x42);CHECK(s_ov5640.base.state==DEVICE_STATE_READY);
 if(id==4){CHECK(Device_Start(&s_ov5640.base)==SYS_OK);CHECK(s_ov5640.base.state==DEVICE_STATE_RUNNING);CHECK(Device_Stop(&s_ov5640.base)==SYS_OK);return 0;}
 if(id==5){fail_reg=-1;CHECK(App_CameraConfigure(&cfg)==SYS_OK);CHECK(App_CameraInit()==SYS_OK);CHECK(dcmi_starts==1);CHECK(g_dma_before_sensor==0);CHECK(App_CameraInitStream()==SYS_ERR_BUSY);return 0;}
 if(id==6){CHECK(Device_Start(&s_ov5640.base)==SYS_OK);CHECK(s_ov5640.ops->configure(&s_ov5640,&cfg)==SYS_ERR_INVALID_STATE);return 0;}
 return 0;
}
