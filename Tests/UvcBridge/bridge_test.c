#include "../../CM7/Application/app_uvc_bridge.c"

static uint32_t ipsr, primask;
static unsigned queries, gets, submits, releases;
static bool can_submit=true, owned, available=true;
static SysResult_t query_result, submit_result, release_result;
static UsbUvcStreamState_t usb_state;
static uint32_t completed;
static uint16_t width=160, height=120;
static CameraPixelFormat_t format=CAMERA_PIXEL_FORMAT_YUV422;
static uint32_t bytes=38400;
static const uint8_t *pixels=(const uint8_t *)0xD0000400U;
static const uint8_t *last_pixels;
static uint32_t last_length;
uint32_t Test_GetIpsr(void){ return ipsr; }
uint32_t Test_GetPrimask(void){ return primask; }
SysResult_t USB_UVC_GetStreamStatus(UsbUvcStreamStatus_t *status)
{
    queries++;
    if (query_result!=SYS_OK) return query_result; /* output intentionally untouched */
    status->state=usb_state;status->result=SYS_ERR_IO;
    status->frame_owned=owned;status->can_submit=can_submit;
    status->completed_frames=completed;
    return SYS_OK;
}
bool CameraCapture_StreamGetFrame(CameraFrame_t *frame)
{
    gets++;
    if (!available) return false;
    frame->width=width;frame->height=height;frame->format=format;
    frame->size_bytes=bytes;frame->data=pixels;
    return true;
}
SysResult_t USB_UVC_SubmitFrame(const uint8_t *data,uint32_t length)
{
    submits++;last_pixels=data;last_length=length;
    return submit_result;
}
SysResult_t CameraCapture_StreamReleaseFrame(void)
{
    releases++;return release_result;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while(0)
static int Acquire(void)
{
    App_UvcBridgeProcess();
    CHECK(s_bridge.state==UVC_BRIDGE_HELD && !s_bridge.release_pending);
    CHECK(queries==1 && gets==1 && !submits && !releases);
    return 0;
}
static int Loan(void)
{
    int r=Acquire();if(r)return r;
    App_UvcBridgeProcess();
    CHECK(s_bridge.state==UVC_BRIDGE_LOANED && submits==1 && !releases);
    CHECK(last_pixels==pixels && last_length==38400);
    CHECK(s_bridge.submitted_frames==1);
    return 0;
}
int Bridge_TestRun(unsigned id)
{
    int r; unsigned i;
    switch(id)
    {
    case 0: /* not ready never takes Media frame */
        can_submit=false;App_UvcBridgeProcess();CHECK(!gets && !submits && !releases);break;
    case 1: /* query failure does not use uninitialized status */
        query_result=SYS_ERR_IO;App_UvcBridgeProcess();CHECK(!gets && !submits);break;
    case 2:
        available=false;App_UvcBridgeProcess();CHECK(gets==1 && s_bridge.state==UVC_BRIDGE_IDLE);break;
    case 3:
        r=Acquire();if(r)return r;
        CHECK(s_bridge.frame.data==pixels);break;
    case 4:
        r=Loan();if(r)return r;break;
    case 5: case 6: case 7: case 8:
        r=Acquire();if(r)return r;
        submit_result=(id==5)?SYS_ERR_BUSY:(id==6)?SYS_ERR_INVALID_STATE:(id==7)?SYS_ERR_IO:SYS_ERR_INVALID_ARG;
        App_UvcBridgeProcess();
        CHECK(submits==1 && releases==1 && s_bridge.state==UVC_BRIDGE_IDLE);
        CHECK(!s_bridge.submitted_frames && s_bridge.rejected_frames==1 && s_bridge.released_frames==1);
        break;
    case 9: /* failed Submit + failed Release cannot resubmit */
        r=Acquire();if(r)return r;
        submit_result=SYS_ERR_BUSY;release_result=SYS_ERR_IO;App_UvcBridgeProcess();
        CHECK(s_bridge.state==UVC_BRIDGE_HELD && s_bridge.release_pending && s_bridge.frame.data==pixels);
        App_UvcBridgeProcess();CHECK(submits==1 && gets==1 && queries==1 && releases==2);
        release_result=SYS_OK;App_UvcBridgeProcess();
        CHECK(s_bridge.state==UVC_BRIDGE_IDLE && releases==3 && s_bridge.released_frames==1);
        break;
    case 10: case 11: case 12: case 13: case 14:
        if(id==10)width=159;
        if(id==11)height=121;
        if(id==12)format=CAMERA_PIXEL_FORMAT_RGB565;
        if(id==13)bytes=38399;
        if(id==14)pixels=NULL;
        App_UvcBridgeProcess();
        CHECK(gets==1 && !submits && releases==1 && s_bridge.state==UVC_BRIDGE_IDLE);
        CHECK(s_bridge.rejected_frames==1);break;
    case 15: /* invalid frame, failed Release retains frame */
        bytes=38404;release_result=SYS_ERR_BUSY;App_UvcBridgeProcess();
        CHECK(s_bridge.state==UVC_BRIDGE_HELD && s_bridge.release_pending && !submits);
        App_UvcBridgeProcess();CHECK(gets==1 && queries==1 && releases==2 && !submits);
        release_result=SYS_OK;App_UvcBridgeProcess();CHECK(s_bridge.state==UVC_BRIDGE_IDLE);break;
    case 16: case 17: case 18: case 19: case 20: case 21:
        r=Loan();if(r)return r;
        owned=true;usb_state=(UsbUvcStreamState_t)(id-16);
        for(i=0;i<5;i++){completed++;App_UvcBridgeProcess();}
        CHECK(!releases && s_bridge.state==UVC_BRIDGE_LOANED && s_bridge.frame.data==pixels);
        CHECK(gets==1 && submits==1);break;
    case 22:
        r=Loan();if(r)return r;
        owned=false;App_UvcBridgeProcess();
        CHECK(releases==1 && s_bridge.state==UVC_BRIDGE_IDLE && s_bridge.frame.data==NULL);
        CHECK(s_bridge.released_frames==1);break;
    case 23: /* safe destruction contract */
        r=Loan();if(r)return r;
        query_result=SYS_ERR_INVALID_STATE;App_UvcBridgeProcess();
        CHECK(releases==1 && s_bridge.state==UVC_BRIDGE_IDLE);break;
    case 24: case 25: case 26:
        r=Loan();if(r)return r;
        query_result=id==24?SYS_ERR_IO:id==25?SYS_ERR_BUSY:SYS_ERR_TIMEOUT;
        App_UvcBridgeProcess();CHECK(!releases && s_bridge.state==UVC_BRIDGE_LOANED);break;
    case 27: /* USB returned, Media release failure */
        r=Loan();if(r)return r;
        release_result=SYS_ERR_INVALID_STATE;App_UvcBridgeProcess();
        CHECK(releases==1 && s_bridge.state==UVC_BRIDGE_HELD && s_bridge.release_pending);
        App_UvcBridgeProcess();CHECK(queries==2 && gets==1 && submits==1 && releases==2);
        release_result=SYS_OK;App_UvcBridgeProcess();CHECK(s_bridge.state==UVC_BRIDGE_IDLE);break;
    case 28: case 29:
        r=Loan();if(r)return r;
        query_result=SYS_ERR_INVALID_STATE;
        if(id==28)ipsr=16;else primask=1;
        App_UvcBridgeProcess();CHECK(queries==1 && !releases && s_bridge.state==UVC_BRIDGE_LOANED);
        ipsr=primask=0;App_UvcBridgeProcess();CHECK(releases==1);break;
    case 30: case 31:
        if(id==30)ipsr=16;else primask=1;
        App_UvcBridgeProcess();CHECK(!queries && !gets && !submits && !releases);break;
    case 32: /* two frames, never reacquire while loaned */
        r=Loan();if(r)return r;
        owned=true;App_UvcBridgeProcess();CHECK(gets==1 && submits==1);
        owned=false;App_UvcBridgeProcess();CHECK(releases==1);
        App_UvcBridgeProcess();App_UvcBridgeProcess();CHECK(gets==2 && submits==2 && releases==1);
        App_UvcBridgeProcess();CHECK(releases==2 && s_bridge.submitted_frames==2 && s_bridge.released_frames==2);break;
    case 33: /* readiness vanished between get and submit */
        r=Acquire();if(r)return r;
        can_submit=false;submit_result=SYS_ERR_INVALID_STATE;App_UvcBridgeProcess();
        CHECK(releases==1 && s_bridge.state==UVC_BRIDGE_IDLE && !s_bridge.submitted_frames);break;
    case 34: /* class absent while IDLE never acquires */
        query_result=SYS_ERR_INVALID_STATE;App_UvcBridgeProcess();CHECK(!gets && !releases);break;
    default:return -1;
    }
    return 0;
}
