//编译真实OLED应用、界面、驱动和Framework，只替换IPC读取及IIC收发
#include "../../CM4/Device/Src/device.c"
#include "../../CM4/Device/Src/oled_device.c"
#include "../../CM4/Application/oled_ui.c"
#include "../../CM4/Application/app_oled.c"
#include "../../CM4/Application/app_main.c"
#include "../../CM4/Framework/framework_timer.c"
#include "../../CM4/Framework/framework_event.c"
#include "../../CM4/Framework/framework_runtime.c"
#include "../../CM4/Framework/framework_init.c"
#include "../../Common/Src/ipc_application.c"

typedef struct { uint32_t checks, fail_line; } TestResult;
volatile TestResult g_result;

//导出仿真画面地址，仅供专项测试生成界面预览
uint32_t g_monitor_frame_address;

//保存测试输入、总线事务和受保护的独立Framebuffer
static IpcApplicationView_t s_view;
static SysResult_t s_view_result, s_submit_result, s_completion;
static uint32_t s_now, s_active, s_ready, s_tx_count, s_view_calls;
static uint32_t s_sync_a4, s_sync_a5;
static uint8_t s_tx[32][129];
static uint32_t s_tx_len[32];
static struct {uint32_t before; OledDevice_t oled; uint32_t after;} s_guard;
static uint8_t s_saved[OLED_BUFFER_SIZE];
static uint32_t s_pixels;
static uint32_t s_fail_pixel;
static SysResult_t s_clear_result;

//为无运行库的ARM仿真提供标准内存及字符串操作
void *memcpy(void *d,const void *s,size_t n){uint8_t *a=d;const uint8_t *b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int c,size_t n){uint8_t *a=d;while(n--)*a++=(uint8_t)c;return d;}
int strcmp(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return (unsigned char)*a-(unsigned char)*b;}
int memcmp(const void *a,const void *b,size_t n){const uint8_t *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
void __aeabi_memcpy(void *d,const void *s,size_t n){volatile uint8_t *a=d;const volatile uint8_t *b=s;while(n--)*a++=*b++;}
void __aeabi_memcpy4(void *d,const void *s,size_t n){__aeabi_memcpy(d,s,n);}
void __aeabi_memset(void *d,size_t n,int c){volatile uint8_t *a=d;while(n--)*a++=(uint8_t)c;}
void __aeabi_memclr(void *d,size_t n){volatile uint8_t *a=d;while(n--)*a++=0;}
void __aeabi_memclr4(void *d,size_t n){__aeabi_memclr(d,n);}
void __aeabi_memclr8(void *d,size_t n){__aeabi_memclr(d,n);}

//只模拟本核时间，不使用硬件SysTick
uint32_t Platform_TimeGetMs(void){return s_now;}

//记录初始化命令，验证正常显存显示且没有全亮测试命令
SysResult_t BSP_IIC_WriteSync(uint8_t addr,uint8_t *data,uint16_t len)
{
    (void)addr;
    if(len==2U && data[0]==0U){if(data[1]==0xA4U)s_sync_a4++;if(data[1]==0xA5U)s_sync_a5++;}
    return SYS_OK;
}

//按真实BSP约定在返回前复制小事务，完成由测试显式驱动
SysResult_t BSP_IIC_WriteAsync(uint8_t addr,const uint8_t *data,uint16_t len)
{
    if(s_submit_result!=SYS_OK)return s_submit_result;
    if(s_active || s_ready)return SYS_ERR_BUSY;
    if(addr!=0x3CU || len>129U || s_tx_count>=32U)return SYS_ERR_INVALID_ARG;
    memcpy(s_tx[s_tx_count],data,len);s_tx_len[s_tx_count++]=len;s_active=1U;return SYS_OK;
}

//没有完成事件时保持等待，不伪造通信成功
bool BSP_IIC_TakeResult(SysResult_t *result)
{
    if(!s_ready)return false;
    *result=s_completion;s_ready=0U;return true;
}

//返回完整IPC副本，测试可修改下一次输入但不能修改本次画面
SysResult_t IPC_Service_GetApplicationStatus(IpcApplicationView_t *view)
{
    s_view_calls++;
    if(s_view_result==SYS_OK)*view=s_view;
    return s_view_result;
}

//失败记录行号，Python逐例读取真实断言结果
#define CHECK(x) do{g_result.checks++;if(!(x)){g_result.fail_line=__LINE__;return 1U;}}while(0)

//建立可显示的真实字段组合，数值由用例逐项改变
static void DefaultView(void)
{
    memset(&s_view,0,sizeof(s_view));s_view.peer_state=IPC_PEER_ONLINE;
    s_view.valid_mask=s_view.fresh_mask=s_view.application.valid_mask=IPC_APP_ALL;
    s_view.application.camera_state=1U;s_view.application.usb_state=1U;
    s_view.application.control_state=3U;s_view.application.width=320U;
    s_view.application.height=240U;s_view.application.format=IPC_IMAGE_JPEG;
    s_view.application.camera_fps_milli=10000U;s_view.application.usb_fps_milli=9500U;
    s_view.application.dropped_frames=12U;
}

//捕获绘图坐标并支持像素写入失败注入
static SysResult_t TestPixel(OledDevice_t *oled,uint16_t x,uint16_t y,bool on)
{
    s_pixels++;
    if(x>=OLED_WIDTH || y>=OLED_HEIGHT)return SYS_ERR_IO;
    if(s_fail_pixel && s_pixels==s_fail_pixel)return SYS_ERR_IO;
    return Oled_DrawPixelImpl(oled,x,y,on);
}

//支持清屏失败注入，同时使用真实设备Framebuffer格式
static SysResult_t TestClear(OledDevice_t *oled)
{
    return s_clear_result==SYS_OK?Oled_ClearImpl(oled):s_clear_result;
}

//测试绘图表只包装错误注入，不替换真实像素算法
static const OledOps_t s_test_ops={TestClear,TestPixel,Oled_RefreshImpl,Oled_ProcessImpl};

//显式完成一笔事务后推进真实主循环
static void Complete(SysResult_t result)
{
    s_active=0U;s_ready=1U;s_completion=result;App_OledRun();
}

//初始化真实Device、Initcall和唯一OLED Timer
static uint32_t Init(void)
{
    CHECK(Framework_InitRuntime()==SYS_OK);
    CHECK(s_app_oled==&s_oled);CHECK(s_sync_a4==1U);CHECK(s_sync_a5==0U);
    CHECK(s_timer_pool_count==1U && s_heap_size==1U);
    CHECK(s_timer_pool[0].config.period_ms==100U);return 0U;
}

//计时入口与正式主循环顺序保持一致
static void Step(uint32_t now){s_now=now;Framework_Run();App_Run();}

//逐笔验证四页命令及像素，不允许访问第4页
static uint32_t Finish(void)
{
    uint32_t i,j,base=s_tx_count-1U;
    for(i=0U;i<8U;i++)
    {
        CHECK(s_active==1U);CHECK(s_oled.refresh_page==i/2U);
        if((i&1U)==0U){CHECK(s_tx_len[base+i]==4U);CHECK(s_tx[base+i][1]==(0xB0U|i/2U));}
        else{CHECK(s_tx_len[base+i]==129U);CHECK(s_tx[base+i][0]==0x40U);
            for(j=0U;j<128U;j++)CHECK(s_tx[base+i][j+1U]==s_saved[(i/2U)*128U+j]);}
        Complete(SYS_OK);
        CHECK(memcmp(s_saved,s_oled.framebuffer,OLED_BUFFER_SIZE)==0);
    }
    CHECK(s_oled.refresh_state==OLED_REFRESH_IDLE);
    CHECK(g_oled_monitor_debug.completed_frames==1U);return 0U;
}

//字体、排版、有效性和通信状态专项
static uint32_t UiCase(uint32_t id)
{
    char lines[4][22]={{0}};
    uint32_t i,j;
    OledDevice_t *oled=&s_guard.oled;
    DefaultView();s_guard.before=0x11223344U;s_guard.after=0x55667788U;
    oled->ops=&s_test_ops;g_monitor_frame_address=(uint32_t)oled->framebuffer;
    if(id==0U){CHECK(OledUi_DrawText(oled,0,0,"A")==SYS_OK);for(i=0;i<5;i++)CHECK(oled->framebuffer[i]==s_ascii_font['A'-32][i]);CHECK(oled->framebuffer[5]==0);CHECK(oled->framebuffer[0]==0x7EU);CHECK(oled->framebuffer[1]==0x11U);}
    else if(id==1U){for(i=32;i<127;i++){char text[2]={(char)i,0};CHECK(Oled_ClearImpl(oled)==SYS_OK);CHECK(OledUi_DrawText(oled,0,24,text)==SYS_OK);for(j=0;j<5;j++)CHECK(oled->framebuffer[384+j]==s_ascii_font[i-32][j]);}}
    else if(id==2U){CHECK(OledUi_DrawText(oled,124,29,"ABCDEFGHIJKLMNOPQRSTUVWXYZ")==SYS_OK);CHECK(s_pixels==12U);}
    else if(id==3U){char text[2]={(char)255,0};CHECK(OledUi_DrawText(oled,0,0,text)==SYS_OK);for(i=0;i<5;i++)CHECK(oled->framebuffer[i]==s_ascii_font['?'-32][i]);}
    else if(id==4U){CHECK(OledUi_Render(NULL,&s_view)==SYS_ERR_INVALID_ARG);CHECK(OledUi_Render(oled,NULL)==SYS_ERR_INVALID_ARG);}
    else if(id==5U){memset(oled->framebuffer,0xAB,512);oled->refresh_state=OLED_REFRESH_WAIT_DATA;CHECK(OledUi_Render(oled,&s_view)==SYS_ERR_BUSY);for(i=0;i<512;i++)CHECK(oled->framebuffer[i]==0xAB);}
    else if(id==6U){oled->ops=NULL;CHECK(OledUi_Render(oled,&s_view)==SYS_ERR_NOT_SUPPORTED);}
    else if(id==7U){s_clear_result=SYS_ERR_IO;CHECK(OledUi_Render(oled,&s_view)==SYS_ERR_IO);CHECK(s_pixels==0);}
    else if(id==8U){s_fail_pixel=20;CHECK(OledUi_Render(oled,&s_view)==SYS_ERR_IO);}
    else if(id==9U){CHECK(OledUi_Render(oled,&s_view)==SYS_OK);s_view.peer_state=IPC_PEER_TIMEOUT;CHECK(OledUi_Render(oled,&s_view)==SYS_OK);for(i=256;i<512;i++)CHECK(oled->framebuffer[i]==0);}
    else if(id>=10U && id<=13U){const char *names[4]={"Y8","RGB565","YUY2","MJPG"};CHECK(strcmp(OledUi_Format(id-9),names[id-10])==0);s_view.application.format=id-9;}
    else if(id>=14U && id<=17U){uint32_t fps[4]={0U,999U,15999U,UINT32_MAX};const char *expect[4]={"0.0","0.9","15.9",">99"};OledUi_AppendFps(lines[0],fps[id-14]);CHECK(strcmp(lines[0],expect[id-14])==0);return 0;}
    else if(id==19U)s_view.application.usb_fps_milli=0;
    else if(id==20U){s_view.application.camera_state=0;s_view.application.usb_state=0;}
    else if(id==21U){s_view.application.camera_state=2;s_view.application.usb_state=2;}
    else if(id==22U)s_view.peer_state=IPC_PEER_STARTING;
    else if(id==23U)s_view.peer_state=IPC_PEER_TIMEOUT;
    else if(id==24U)s_view.peer_state=IPC_PEER_STARTUP_TIMEOUT;
    else if(id==25U)s_view.peer_state=IPC_PEER_PROTOCOL_ERROR;
    else if(id==26U)s_view.peer_state=99;
    else if(id==27U)s_view.read_result=SYS_ERR_IO;
    else if(id==28U)s_view.read_result=SYS_ERR_BUSY;
    else if(id==29U)s_view.valid_mask=0;
    else if(id>=30U && id<=32U)s_view.valid_mask&=~(1U<<(id-30));
    else if(id>=33U && id<=35U)s_view.fresh_mask&=~(1U<<(id-33));
    else if(id==36U)s_view.fresh_mask=0;
    else if(id==37U)s_view.application.camera_result=SYS_ERR_IO;
    else if(id==38U)s_view.application.usb_result=SYS_ERR_TIMEOUT;
    else if(id==39U){s_view.application.control_state=6;s_view.application.control_result=SYS_ERR_IO;}
    else if(id==40U){IpcApplicationCache_t cache={0};IPC_Application_Observe(&cache,&s_view.application,0xfffffff0U);CHECK(IPC_Application_GetView(&cache,0x1e3U,IPC_PEER_ONLINE,SYS_OK,&s_view)==SYS_OK);CHECK(s_view.fresh_mask==IPC_APP_ALL);CHECK(IPC_Application_GetView(&cache,0x1e4U,IPC_PEER_ONLINE,SYS_OK,&s_view)==SYS_OK);CHECK(s_view.fresh_mask==0);}
    else if(id==41U){s_view.application.dropped_frames=UINT32_MAX;s_view.application.camera_fps_milli=UINT32_MAX;s_view.application.usb_fps_milli=UINT32_MAX;}
    else if(id==42U){OledUi_AppendResult(lines[0],(-2147483647-1));CHECK(strcmp(lines[0],"-2147483648")==0);return 0;}
    OledUi_BuildLines(&s_view,lines);
    if(id==18U || id==28U){CHECK(strcmp(lines[0],"CAM 320x240 MJPG")==0);CHECK(strcmp(lines[1],"CAP 10.0 USB 9.5")==0);CHECK(strcmp(lines[2],"DROP 12")==0);CHECK(strcmp(lines[3],"M7 OK CTRL RUN")==0);}
    if(id==19U)CHECK(strcmp(lines[1],"CAP 10.0 USB WAIT")==0);
    if(id==20U)CHECK(strcmp(lines[1],"CAP STOP USB STOP")==0);
    if(id==21U)CHECK(strcmp(lines[1],"CAP HALT USB HALT")==0);
    if(id==22U)CHECK(strcmp(lines[0],"IPC INITIALIZING")==0);
    if(id==23U || id==24U)CHECK(strcmp(lines[0],"M7 OFFLINE")==0);
    if(id==25U)CHECK(strcmp(lines[0],"IPC PROTOCOL ERROR")==0);
    if(id==26U)CHECK(strcmp(lines[0],"IPC INVALID STATE")==0);
    if(id==27U)CHECK(strcmp(lines[0],"IPC READ ERROR")==0);
    if(id==29U)CHECK(strcmp(lines[0],"DATA INVALID")==0);
    if(id==30U)CHECK(strcmp(lines[0],"CAM INVALID")==0);
    if(id==31U)CHECK(strcmp(lines[1],"CAP 10.0 USB --")==0);
    if(id==32U)CHECK(strcmp(lines[3],"M7 OK CTRL INVALID")==0);
    if(id==33U || id==36U || id==40U)CHECK(strcmp(lines[0],"CAM STALE")==0);
    if(id==33U)CHECK(strcmp(lines[1],"CAP STALE USB 9.5")==0);
    if(id==36U || id==40U)CHECK(strcmp(lines[1],"CAP STALE USB STALE")==0);
    if(id==23U || id==24U || id==25U){CHECK(lines[2][0]==0);CHECK(lines[3][0]==0);}
    if(id==34U)CHECK(strcmp(lines[1],"CAP 10.0 USB STALE")==0);
    if(id==35U)CHECK(strcmp(lines[3],"M7 OK CTRL STALE")==0);
    if(id==37U)CHECK(strcmp(lines[3],"FAULT CAM -9")==0);
    if(id==38U)CHECK(strcmp(lines[3],"FAULT USB -8")==0);
    if(id==39U)CHECK(strcmp(lines[3],"FAULT CTRL -9")==0);
    if(id==41U){CHECK(strcmp(lines[1],"CAP >99 USB >99")==0);CHECK(strcmp(lines[2],"DROP 4294967295")==0);}
    for(i=0;i<4;i++)CHECK(lines[i][21]==0);
    if(id!=6U && id!=7U && id!=8U && id!=5U)CHECK(OledUi_Render(oled,&s_view)==SYS_OK);
    CHECK(s_guard.before==0x11223344U && s_guard.after==0x55667788U);return 0;
}

//不执行任何实际操作的Timer回调，用于占满固定对象池
static void Noop(void *context){(void)context;}

//应用刷新、失败回滚和Timer生命周期专项
static uint32_t AppCase(uint32_t id)
{
    uint32_t i,n;
    DefaultView();
    if(id==44U){App_OledRun();CHECK(s_view_calls==0 && s_tx_count==0);return 0;}
    if(id==54U){FrameworkTimerConfig_t c={100,FRAMEWORK_TIMER_PERIODIC,Noop,NULL};FrameworkTimerHandle_t h;for(i=0;i<16;i++)CHECK(Framework_TimerCreate(&c,&h)==SYS_OK);CHECK(Framework_InitRuntime()==SYS_ERR_NO_RESOURCE);CHECK(s_app_oled==NULL);return 0;}
    if(id==55U){CHECK(App_OledInit()==SYS_ERR_NOT_FOUND);return 0;}
    if(id==56U){CHECK(Device_RegisterAll()==SYS_OK);CHECK(App_OledInit()==SYS_ERR_INVALID_STATE);return 0;}
    if(id==57U){s_oled.ops=NULL;CHECK(Framework_InitRuntime()==SYS_ERR_NOT_SUPPORTED);CHECK(s_timer_pool_count==0);return 0;}
    if(id==58U){s_guard.oled.ops=&s_test_ops;CHECK(OledUi_DrawText(&s_guard.oled,0,32,"A")==SYS_OK);CHECK(s_pixels==0);return 0;}
    if(id==59U){s_guard.oled.ops=&s_test_ops;CHECK(OledUi_DrawText(&s_guard.oled,65535,65535,"A")==SYS_OK);CHECK(s_pixels==0);return 0;}
    CHECK(Init()==0);
    if(id==43U){FrameworkTimerHandle_t h=s_oled_timer;CHECK(App_OledInit()==SYS_OK);CHECK(s_oled_timer==h);CHECK(s_timer_pool_count==1);return 0;}
    if(id==60U){CHECK(s_sync_a4==1 && s_sync_a5==0);return 0;}
    if(id==47U || id==48U){memset(s_oled.framebuffer,0x37,512);s_submit_result=id==47U?SYS_ERR_BUSY:SYS_ERR_IO;Step(100);for(i=0;i<512;i++)CHECK(s_oled.framebuffer[i]==0x37);CHECK(s_oled.refresh_state==OLED_REFRESH_IDLE);CHECK(s_tx_count==0);CHECK(g_oled_monitor_debug.last_error==(id==47U?SYS_OK:SYS_ERR_IO));s_submit_result=SYS_OK;Step(200);CHECK(s_tx_count==1);return 0;}
    if(id==52U){s_view_result=SYS_ERR_IO;Step(100);CHECK(s_tx_count==0);CHECK(g_oled_monitor_debug.ipc_errors==1);return 0;}
    if(id==53U){s_oled.ops=&s_test_ops;s_fail_pixel=30;memset(s_oled.framebuffer,0x27,512);Step(100);for(i=0;i<512;i++)CHECK(s_oled.framebuffer[i]==0x27);CHECK(s_tx_count==0);CHECK(g_oled_monitor_debug.render_errors==1);return 0;}
    Step(99);CHECK(s_tx_count==0);Step(100);CHECK(s_tx_count==1);CHECK(s_view_calls==1);
    memcpy(s_saved,s_oled.framebuffer,512);
    if(id==45U){CHECK(g_oled_monitor_debug.submitted_frames==1);return 0;}
    if(id==46U){n=s_tx_count;for(i=2;i<5;i++){Step(i*100);CHECK(memcmp(s_saved,s_oled.framebuffer,512)==0);CHECK(s_view_calls==1);CHECK(s_tx_count==n);}CHECK(g_oled_monitor_debug.busy_skips==3);return 0;}
    if(id==49U){Complete(SYS_ERR_IO);CHECK(s_oled.refresh_state==OLED_REFRESH_IDLE);CHECK(s_oled_error==SYS_ERR_IO);CHECK(g_oled_monitor_debug.completed_frames==0);Step(200);CHECK(s_tx_count==2);CHECK(s_oled_error==SYS_ERR_IO);return 0;}
    if(id==50U || id==51U){if(id==51U){s_view.peer_state=IPC_PEER_TIMEOUT;Step(200);CHECK(s_view_calls==1);CHECK(memcmp(s_saved,s_oled.framebuffer,512)==0);}CHECK(Finish()==0);if(id==51U){Step(300);CHECK(s_view_calls==2);CHECK(memcmp(s_saved,s_oled.framebuffer,512)!=0);}return 0;}
    return 1;
}

//每个用例由仿真器重新装载ELF，静态状态不会串扰
uint32_t OledMonitor_TestRun(uint32_t id)
{
    return id<=42U?UiCase(id):AppCase(id);
}
