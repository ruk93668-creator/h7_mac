/* State-seeded ownership witnesses, not a complete UVC descriptor/stream test. */
#include "../../ThirdParty/tinyusb/src/class/video/video_device.c"
volatile uint32_t g_video_result[8];
static uint8_t source_frame[160U*120U*2U];
void Video_Run(uint32_t which)
{
 videod_streaming_interface_t *s=&_videod_streaming_itf[0];videod_init();
 g_video_result[0]=sizeof(video_probe_and_commit_control_t);
 g_video_result[1]=offsetof(video_probe_and_commit_control_t,bFormatIndex);
 g_video_result[2]=offsetof(video_probe_and_commit_control_t,bmLayoutPerStream);
 /* The input models a frame already held by the real class; it is not an accepted submit proof. */
 s->buffer=source_frame;s->bufsize=sizeof(source_frame);s->offset=128;
 if(which==1)videod_reset(0);
 else if(which==2)(void)videod_deinit();
 else if(which==3)_finish_frame(s,true);
 g_video_result[3]=s->buffer!=NULL;g_video_result[4]=s->bufsize;
}
