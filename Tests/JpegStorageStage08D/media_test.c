//原五缓冲90项断言保持不变，JPEG接口只作不可达检测
#include "../CameraStreamState/stream_test.c"
static uint32_t unexpected_jpeg;
SysResult_t CameraMjpegFifo_Start(const CameraConfig_t *c){(void)c;unexpected_jpeg++;return SYS_ERR_NOT_SUPPORTED;}
SysResult_t CameraMjpegFifo_Process(void){unexpected_jpeg++;return SYS_ERR_NOT_SUPPORTED;}
bool CameraMjpegFifo_GetFrame(CameraFrame_t *f){(void)f;unexpected_jpeg++;return false;}
SysResult_t CameraMjpegFifo_ReleaseFrame(void){unexpected_jpeg++;return SYS_ERR_NOT_SUPPORTED;}
bool CameraMjpegFifo_HasLoan(void){unexpected_jpeg++;return false;}
void CameraMjpegFifo_OnStopped(void){unexpected_jpeg++;}
void CameraMjpegFifo_GetStats(uint32_t *a,uint32_t *b){(void)a;(void)b;unexpected_jpeg++;}
int Stage02_TestRun(uint32_t id){int r=Stream_TestRun(id);return r!=0?r:unexpected_jpeg!=0?__LINE__:0;}
