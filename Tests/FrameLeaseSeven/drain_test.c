void __DMB(void);
//真实CameraCapture和FIFO共同执行，验证双消费者DRAINING
#include "drain_stream.inc"
#include "../../CM7/Media/Src/camera_mjpeg_fifo.c"
SysResult_t BSP_DCMI_StartJpegContinuous(uint32_t *a,uint32_t *b,uint32_t n,const BspDcmiByteStreamOps_t *ops,void *ctx)
{(void)a;(void)b;(void)n;(void)ops;(void)ctx;return SYS_OK;}
int Stage02_TestRun(uint32_t id)
{
 CameraConfig_t c={320,240,CAMERA_PIXEL_FORMAT_JPEG};CameraFrame_t f;CameraMjpegLease_t l;
 EQ(CameraMjpegFifo_Start(&c),SYS_OK);
 s_fifo.ready_count=1;s_fifo.ready[0].offset=2*CAMERA_MJPEG_BLOCK_BYTES;
 s_fifo.ready[0].length=100;s_fifo.ready[0].blocks=UINT64_C(1)<<2;
 s_fifo.blocks[2].state=MJPEG_BLOCK_SEALED;s_fifo.blocks[2].references=1;s_fifo.blocks[2].parsed=true;
 s_capture.initialized=true;s_capture.config=c;s_capture.mode=CAMERA_CAPTURE_MODE_STREAM;s_capture.state=CAMERA_CAPTURE_CAPTURING;
 EQ(CameraCapture_StreamGetFrame(&f),true);EQ(CameraMjpegFifo_RetainFrame(&f,&l),SYS_OK);
 if(id==0){EQ(CameraCapture_Stop(),SYS_ERR_BUSY);EQ(stop_calls,0);return 0;}
 s_capture.state=CAMERA_CAPTURE_ERROR;
 if(id==1){stop_result=SYS_ERR_IO;EQ(CameraCapture_Stop(),SYS_ERR_IO);EQ(s_capture.state,CAMERA_CAPTURE_ERROR);EQ(CameraMjpegFifo_HasLoan(),true);return 0;}
 EQ(CameraCapture_Stop(),SYS_OK);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(stop_calls,1);
 if(id==2){EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(CameraCapture_Stop(),SYS_ERR_BUSY);EQ(CameraMjpegFifo_ReleaseLease(&l),SYS_OK);EQ(CameraCapture_Stop(),SYS_OK);EQ(CameraCapture_IsStopped(),true);EQ(stop_calls,1);return 0;}
 if(id==3){EQ(CameraMjpegFifo_ReleaseLease(&l),SYS_OK);EQ(s_capture.state,CAMERA_CAPTURE_DRAINING);EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);EQ(CameraCapture_IsStopped(),true);return 0;}
 if(id==4){EQ(CameraCapture_StreamReleaseFrame(),SYS_OK);l.id++;EQ(CameraMjpegFifo_ReleaseLease(&l),SYS_ERR_INVALID_STATE);EQ(CameraCapture_Stop(),SYS_ERR_BUSY);return 0;}
 return __LINE__;
}

void __DMB(void) {}
