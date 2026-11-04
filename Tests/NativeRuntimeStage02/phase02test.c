#define APP_VIDEO_TEST_MODE 1
#define Stage02_TestRun Stage02Uvc_TestRun
#include "phase02d.c"
#undef Stage02_TestRun
int Stage02_TestRun(uint32_t id)
{
    uint32_t i;
    EQ(Prepare(),0);Control();Cam();Control();Usb();Control();
    EQ(s_control_state,CONTROL_RUNNING);EQ(start_count,1);EQ(dcmi_starts,0);EQ(sensor_starts,0);
    EQ(s_capture.initialized,false);EQ(s_uvc_active,NULL);EQ(s_bridge_configured,false);
    if(id==0)return 0;
    Select(RUNTIME_TARGET_USB);HAL_PCD_ResetCallback(&s_pcd);Configured();
    if(id==1){EQ(USB_Device_IsConfigured(),true);return 0;}
    if(id==2){for(i=0;i<100;i++){Sof();Cam();}EQ(g_bridge_debug.acquired_frames,0);EQ(USB_UVC_GetCompletedFrameCount(),0);return 0;}
    if(id==3){EQ(Stop(),0);EQ(sensor_stops,0);return 0;}
    if(id==4){Request(0x80,USB_REQ_GET_DESCRIPTOR,0x0100,0,18);EQ(s_endpoints[1][0].busy,true);EQ(s_endpoints[1][0].stalled,false);return 0;}
    if(id==5){Request(0,USB_REQ_SET_ADDRESS,7,0,0);EQ(s_device.dev_address,7);return 0;}
    if(id==6){Select(RUNTIME_TARGET_USB);HAL_PCD_DisconnectCallback(&s_pcd);Usb();EQ(s_uvc_active,NULL);EQ(fake_dma_active,false);return 0;}
    Select(RUNTIME_TARGET_USB);HAL_PCD_ResetCallback(&s_pcd);EQ(s_endpoints[0][0].opened,true);EQ(s_endpoints[1][0].opened,true);return 0;
}
