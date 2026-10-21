from pathlib import Path
import shutil

here = Path(__file__).resolve().parent / 'Camera'
here.mkdir(exist_ok=True)
root = here.parents[2]
source = root / 'Tests/CameraConfig/Camera'
shutil.copytree(source / 'stubs', here / 'stubs', dirs_exist_ok=True)
shutil.copyfile(source / 'test.sct', here / 'test.sct')
text = (source / 'camera_test.c').read_text(encoding='utf-8')
text = text[:text.index('/* Sensor regression: capture transport stays idle; no synthetic frame. */')]
marker = 'static const CameraConfig_t *observed_config;'
assert marker in text
mock = '''static uint32_t mock_starts,mock_stops,mock_takes,mock_flags,mock_words,mock_address;
static SysResult_t mock_start_result,mock_stop_result,mock_event_result;
SysResult_t BSP_DCMI_StartSnapshot(uint32_t *buffer,uint32_t words)
{
 mock_starts++;mock_words=words;mock_address=(uint32_t)buffer;return mock_start_result;
}
SysResult_t BSP_DCMI_Stop(void){mock_stops++;return mock_stop_result;}
bool BSP_DCMI_TakeEvents(BspDcmiEvents_t *events)
{
 mock_takes++;
 if(!mock_flags)return false;
 events->flags=mock_flags;events->result=mock_event_result;events->dcmi_error=events->dma_error=0;
 mock_flags=0;return true;
}
static int CleanupCases(uint32_t id)
{
 uint32_t i,n;
 CameraFrame_t frame;
 if(id==54){App_Run();EQ(mock_starts,0);EQ(mock_stops,0);EQ(mock_takes,0);return 0;}
 EQ(Framework_InitRuntime(),SYS_OK);EQ(writes,272);EQ(s_camera_initialized,true);
 EQ(s_capture.initialized,true);EQ(s_capture.state,CAMERA_CAPTURE_IDLE);
 EQ(s_capture.frame_bytes,19200);EQ(s_capture.valid_bytes,0);
 EQ(mock_starts,0);EQ(mock_stops,0);EQ(mock_takes,0);
 if(id==55 || id==56){
  n=writes;
  for(i=0;i<1000;i++){Framework_Run();App_Run();if(id==56)EQ(App_Init(),SYS_OK);}
  EQ(writes,n);EQ(mock_starts,0);EQ(mock_stops,0);EQ(mock_takes,0);
  EQ(s_capture.state,CAMERA_CAPTURE_IDLE);return 0;
 }
 if(id==58)mock_start_result=SYS_ERR_IO;
 EQ(CameraCapture_Start(),id==58?SYS_ERR_IO:SYS_OK);
 EQ(mock_starts,1);EQ(mock_words,4800);EQ(mock_address,0xD0000400UL);
 if(id==58){
  EQ(s_capture.state,CAMERA_CAPTURE_ERROR);App_Run();EQ(mock_stops,1);
  EQ(s_capture.state,CAMERA_CAPTURE_IDLE);EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);
  for(i=0;i<10;i++)App_Run();EQ(mock_starts,1);return 0;
 }
 if(id==59){
  mock_flags=BSP_DCMI_EVENT_ERROR;mock_event_result=SYS_ERR_IO;mock_stop_result=SYS_ERR_IO;
  App_Run();EQ(mock_stops,1);EQ(s_capture.state,CAMERA_CAPTURE_ERROR);
  EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);EQ(CameraCapture_GetFrame(&frame),false);
  mock_stop_result=SYS_OK;App_Run();EQ(mock_stops,2);EQ(s_capture.state,CAMERA_CAPTURE_IDLE);
  EQ(CameraCapture_GetLastResult(),SYS_ERR_IO);EQ(mock_starts,1);return 0;
 }
 if(id==60){
  now=s_capture.start_tick+100;App_Run();EQ(mock_stops,1);
  EQ(s_capture.state,CAMERA_CAPTURE_IDLE);EQ(CameraCapture_GetLastResult(),SYS_ERR_TIMEOUT);
  EQ(CameraCapture_GetFrame(&frame),false);EQ(mock_starts,1);return 0;
 }
 mock_flags=BSP_DCMI_EVENT_FRAME;App_Run();EQ(s_capture.state,CAMERA_CAPTURE_READY);
 EQ(s_capture.valid_bytes,19200);EQ(CameraCapture_GetFrame(&frame),true);
 EQ(frame.size_bytes,19200);EQ((uint32_t)frame.data,0xD0000400UL);
 n=mock_takes;
 for(i=0;i<100;i++){App_Run();if(id==61)EQ(App_Init(),SYS_OK);}
 EQ(s_capture.state,CAMERA_CAPTURE_READY);EQ(mock_takes,n);EQ(mock_starts,1);
 EQ(CameraCapture_ReleaseFrame(),SYS_OK);EQ(s_capture.state,CAMERA_CAPTURE_IDLE);
 App_Run();EQ(mock_starts,1);EQ(mock_stops,0);return 0;
}

'''
text = text.replace(marker, mock + marker)
marker = ' EQ(BSP_Camera_Init(),SYS_OK);'
assert text.count(marker) == 1
text = text.replace(marker, ' if(id>=54)return CleanupCases(id);\n'+marker)
(here / 'camera_test.c').write_text(text, encoding='utf-8')
runner = (source / 'run_tests.py').read_text(encoding='utf-8')
runner = runner.replace('Tests/CameraConfig/Camera/stubs', 'Tests/CameraCleanup/Camera/stubs')
runner = runner.replace('commands=[]', '''names += ['未初始化轮询无副作用', '初始化及千次轮询不自动采集', '重复初始化不重配不启动',
          '显式启动后完成帧交给业务释放', '显式启动失败仍由Media恢复', '恢复失败由Media内部重试',
          '采集超时由Media处理', '重复初始化不回收业务持有的帧']
commands=[]''', 1)
runner = runner.replace(" if id>=42: expected=", " if id>=54: expected='清理后Application只初始化和推进Media；不自动Start/GetFrame/Release；错误恢复留在Media；不读取SDRAM像素'\n elif id>=42: expected=")
(here / 'run_tests.py').write_text(runner, encoding='utf-8')
print('54 camera/config regressions plus 8 production-cleanup cases prepared.')
