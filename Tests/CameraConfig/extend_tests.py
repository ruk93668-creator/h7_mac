from pathlib import Path

here = Path(__file__).resolve().parent / 'Camera'
test = (here / 'camera_test.c').read_text(encoding='utf-8')
test = test.replace('s_camera_ops.configure_y8_colorbar(NULL)',
                    's_camera_ops.configure(NULL, App_CameraGetConfig())')
test = test.replace('s_camera_ops.configure_y8_colorbar(&s_ov5640)',
                    's_camera_ops.configure(&s_ov5640, App_CameraGetConfig())')
test = test.replace('static int Cases(uint32_t id)', '''static const CameraConfig_t *observed_config;
static SysResult_t Spy_Configure(Ov5640Device_t *camera,const CameraConfig_t *config)
{
 observed_config=config;
 return s_camera_ops.configure(camera,config);
}

static int Cases(uint32_t id)''')
marker = ' if(id==34){'
assert marker in test
test = test.replace(marker, ''' if(id>=42){
  CameraConfig_t config=*App_CameraGetConfig();
  const Ov5640Ops_t spy_ops={Ov5640_ReadId,Spy_Configure};
  uint32_t tx_before=g_result.tx_count;
  EQ(App_CameraGetConfig()==App_CameraGetConfig(),1);
  EQ(config.width,160);EQ(config.height,120);EQ(config.format,CAMERA_PIXEL_FORMAT_Y8);
  if(id==42){EQ(writes,0);return 0;}
  EQ(Device_RegisterAll(),SYS_OK);EQ(Device_InitAll(),SYS_OK);
  if(id==52){
   s_ov5640.ops=&spy_ops;EQ(App_Init(),SYS_OK);
   EQ(observed_config==App_CameraGetConfig(),1);EQ(writes,272);return 0;
  }
  if(id==43){EQ(s_camera_ops.configure(&s_ov5640,NULL),SYS_ERR_INVALID_ARG);EQ(writes,0);return 0;}
  if(id==44)config.width=0;
  if(id==45)config.height=0;
  if(id==46)config.width=320;
  if(id==47)config.height=240;
  if(id==48)config.format=(CameraPixelFormat_t)1;
  if(id==49)config.format=(CameraPixelFormat_t)-1;
  if(id==50){config.width=320;config.height=240;}
  if(id==53){config.width=65535;config.height=65535;nack_index=0;}
  result=s_camera_ops.configure(&s_ov5640,&config);
  EQ(result,id==51?SYS_OK:((id==44 || id==45)?SYS_ERR_INVALID_ARG:SYS_ERR_NOT_SUPPORTED));
  EQ(s_ov5640.base.state,DEVICE_STATE_READY);
  if(id==51){
   EQ(writes,272);EQ(config.width,160);EQ(config.height,120);EQ(config.format,CAMERA_PIXEL_FORMAT_Y8);
   EQ(registers[0x3808],0);EQ(registers[0x3809],160);EQ(registers[0x380A],0);EQ(registers[0x380B],120);
   EQ(registers[0x503D],0x80);return 0;
  }
  EQ(writes,0);EQ(g_result.tx_count,tx_before);return 0;
 }
''' + marker)
(here / 'camera_test.c').write_text(test, encoding='utf-8')

runner = (here / 'run_tests.py').read_text(encoding='utf-8')
runner = runner.replace('commands=[]', '''names += ['只读配置来源及取值', 'NULL配置拒绝', '零宽度拒绝', '零高度拒绝',
          '未支持宽度拒绝', '未支持高度拒绝', '未知格式拒绝', '负数格式拒绝',
          '未支持分辨率组合拒绝', '有效配置不被Device修改', 'Application向Device传递同一配置指针',
          '未支持配置先于SCCB访问拒绝']
commands=[]''', 1)
runner = runner.replace(" if 3<=id<=19: expected=", " if id>=42: expected='公共配置取值、参数拒绝、不支持语义、无副作用或同一指针传递断言成立'\n elif 3<=id<=19: expected=")
(here / 'run_tests.py').write_text(runner, encoding='utf-8')
print('42 existing camera cases migrated to config API; 12 config cases added.')
