from pathlib import Path
import shutil
r=Path('.');t=r/'Tests/Ov5640ProductionCleanup';old=r/'Tests/Ov5640Y8'
shutil.copytree(old/'stubs',t/'stubs',dirs_exist_ok=True);shutil.copyfile(old/'test.sct',t/'test.sct')
s=(old/'camera_test.c').read_text(encoding='utf-8');s=s[:s.index('static int Cases(')]
s=s.replace('static uint32_t writes,verify_active,fail_read;','static uint32_t writes,verify_active,fail_read,verify_reads;')
s=s.replace('phase=2;g_result.reads++;','phase=2;g_result.reads++;if(verify_active)verify_reads++;')
table=(t/'register_verify_table.txt').read_text(encoding='utf-8').replace('s_verify[OV5640_VERIFY_COUNT]','s_test_verify[TEST_VERIFY_COUNT]')
s+='''//回读验收仅存在于独立测试，不进入正式固件
#define TEST_VERIFY_COUNT 17U

typedef struct
{
 uint16_t failed_reg;
 uint8_t expected,actual,checked_count;
 uint8_t values[TEST_VERIFY_COUNT];
} TestVerifyReport;

'''+table+'''
static SysResult_t Test_VerifyRegisters(TestVerifyReport *report)
{
 uint32_t i;
 SysResult_t result;
 for(i=0;i<TEST_VERIFY_COUNT;i++){
  report->failed_reg=s_test_verify[i].reg;
  report->expected=s_test_verify[i].value;
  result=BSP_Camera_ReadReg(s_test_verify[i].reg,&report->values[i]);
  if(result!=SYS_OK)return result;
  report->actual=report->values[i];
  if(report->actual!=report->expected)return SYS_ERR_PROTOCOL;
  report->checked_count++;
 }
 report->failed_reg=0;
 return SYS_OK;
}

static int Cases(uint32_t id)
{
 uint32_t i,n;
 uint8_t value=0,bytes[2]={0xA5,0x5A};
 TestVerifyReport report={0};
 SysResult_t result,verified;
 clock_q=240000000;host_sda=host_scl=1;sensor_id=0x5640;nack_index=-1;mismatch_reg=fail_reg=-1;
 if(id==25){EQ(BSP_Camera_WriteReg(0x1234,0xAB),SYS_ERR_INVALID_STATE);EQ(writes,0);return 0;}
 if(id==35)clock_q=1;
 if(id==35 || id==36){EQ(Framework_InitRuntime(),id==35?SYS_ERR_INVALID_STATE:SYS_ERR_NOT_SUPPORTED);EQ(writes,0);return 0;}
 EQ(BSP_Camera_Init(),SYS_OK);
 if(id>=20 && id<=24){
  if(id<24)nack_index=(int32_t)id-20;
  result=BSP_Camera_WriteReg(0x1234,0xAB);
  EQ(result,id==24?SYS_OK:SYS_ERR_IO);EQ(bus_active,0);EQ(host_sda,1);EQ(host_scl,1);
  if(id==24){EQ(registers[0x1234],0xAB);EQ(BSP_Camera_ReadReg(0x1234,&value),SYS_OK);EQ(value,0xAB);}
  else{EQ(s_sccb_error,id==23?0x40:1U<<(id-18));EQ(writes,0);}
  EQ(g_wire_tx[0],0x78);
  if(id>=21)EQ(g_wire_tx[1],0x12);
  if(id>=22)EQ(g_wire_tx[2],0x34);
  if(id>=23)EQ(g_wire_tx[3],0xAB);
  return 0;
 }
 if(id==26 || id==27){stuck_sda=id==26;stuck_scl=id==27;EQ(BSP_Camera_WriteReg(0x1234,0xAB),SYS_ERR_BUSY);EQ(writes,0);return 0;}
 if(id==28){n=now;EQ(BSP_Camera_WriteReg(0x3008,0x82),SYS_OK);EQ(now-n,5);EQ(g_result.delays,3);return 0;}
 if(id==29){
  s_bus_result=SYS_OK;EQ(Ov5640_BusWrite(0x78,0x1234,bytes,2),SYS_OK);EQ(registers[0x1234],0xA5);EQ(registers[0x1235],0x5A);
  bytes[0]=bytes[1]=0;EQ(Ov5640_BusRead(0x78,0x1234,bytes,2),SYS_OK);EQ(bytes[0],0xA5);EQ(bytes[1],0x5A);
  EQ(Ov5640_BusRead(0x78,0xFFFF,bytes,2),SYS_ERR_INVALID_ARG);s_bus_result=SYS_OK;
  EQ(Ov5640_BusRead(0x79,0,bytes,1),SYS_ERR_INVALID_ARG);s_bus_result=SYS_OK;
  EQ(Ov5640_BusRead(0x78,0,NULL,1),SYS_ERR_INVALID_ARG);s_bus_result=SYS_OK;
  EQ(Ov5640_BusRead(0x78,0,bytes,0),SYS_ERR_INVALID_ARG);return 0;
 }
 if(id==34){
  EQ(s_camera_ops.configure_y8_colorbar(NULL),SYS_ERR_INVALID_ARG);
  EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640),SYS_ERR_INVALID_STATE);
  s_ov5640.base.state=DEVICE_STATE_BUSY;EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640),SYS_ERR_BUSY);EQ(writes,0);return 0;
 }
 if(id==37){nack_index=3;EQ(BSP_Camera_ReadReg(0x300A,&value),SYS_ERR_IO);EQ(s_sccb_error,0x20);return 0;}
 if(id==39){EQ(App_Init(),SYS_ERR_NOT_FOUND);EQ(s_camera_initialized,0);EQ(writes,0);return 0;}
 if(id==40 || id==41){
  const Ov5640Ops_t empty_ops={Ov5640_ReadId,NULL};
  EQ(Device_RegisterAll(),SYS_OK);EQ(Device_InitAll(),SYS_OK);
  s_ov5640.ops=id==40?NULL:&empty_ops;
  EQ(App_Init(),SYS_ERR_NOT_SUPPORTED);EQ(s_camera_initialized,0);EQ(writes,0);
  s_ov5640.ops=&s_camera_ops;EQ(App_Init(),SYS_OK);EQ(s_camera_initialized,1);return 0;
 }
 if(id==1)sensor_id=0x1234;
 if(id>=3 && id<=19)mismatch_reg=s_test_verify[id-3].reg;
 if(id==30 || id==32)fail_reg=0x3036;
 if(id==31)fail_read=1;
 if(id==2)nack_index=0;
 result=Framework_InitRuntime();g_result.init_result=result;
 EQ(s_ov5640.base.state,DEVICE_STATE_READY);
 EQ(s_heap_size,0);EQ(s_timer_pool_count,0);EQ(gpio_b_calls,1);
 //正式初始化不再执行彩条开启后的验收读事务
 EQ(verify_reads,0);
 if(id==1){EQ(result,SYS_ERR_PROTOCOL);EQ(s_camera_initialized,0);EQ(writes,0);return 0;}
 if(id==2 || id==30 || id==32){
  EQ(result,SYS_ERR_IO);EQ(s_camera_initialized,0);
  if(id!=32)return 0;
  fail_reg=-1;EQ(App_Init(),SYS_OK);
 }
 else EQ(result,SYS_OK);
 EQ(s_camera_initialized,1);EQ(verify_reads,0);
 //生产路径结束后，由隔离测试主动回读17个寄存器
 verified=Test_VerifyRegisters(&report);
 g_result.passed=(uint32_t)verified;g_result.failed=report.checked_count;g_result.done=report.failed_reg;
 if(id>=3 && id<=19){
  EQ(verified,SYS_ERR_PROTOCOL);EQ(report.failed_reg,s_test_verify[id-3].reg);EQ(report.checked_count,id-3);EQ(s_camera_initialized,1);return 0;
 }
 if(id==31){EQ(verified,SYS_ERR_IO);EQ(report.checked_count,0);EQ(s_camera_initialized,1);return 0;}
 EQ(verified,SYS_OK);EQ(report.checked_count,TEST_VERIFY_COUNT);
 EQ(((uint16_t)report.values[0]<<8)|report.values[1],160);
 EQ(((uint16_t)report.values[2]<<8)|report.values[3],120);
 for(i=0;i<TEST_VERIFY_COUNT;i++)EQ(registers[s_test_verify[i].reg],s_test_verify[i].value);
 EQ(registers[0x3037],0x03);EQ(registers[0x3008],0x02);
 n=writes;
 for(i=0;i<100;i++){Framework_Run();App_Run();}
 EQ(App_Init(),SYS_OK);EQ(writes,n);EQ(s_heap_size,0);EQ(s_timer_pool_count,0);
 return 0;
}
int Camera_TestRun(uint32_t id)
{
 int result=Cases(id);
 g_result.count=writes;g_result.heap=s_heap_size;g_result.last_bus_error=s_sccb_error;
 return result;
}
'''
(t/'camera_test.c').write_text(s,encoding='utf-8')
s=(old/'run_tests.py').read_text(encoding='utf-8')
s=s.replace('Tests/Ov5640Y8/stubs','Tests/Ov5640ProductionCleanup/stubs')
s=s.replace('commands=[]',"names+=['应用缺少摄像头设备','应用Ops缺失及恢复','应用配置函数缺失及恢复']\ncommands=[]",1)
s=s.replace("'reserved1','reserved2','reserved3'","'verify_result_raw','verified_count','failed_reg'")
a=s.index(' actual=f');b=s.index('\n observed=',a)
s=s[:a]+''' verified=state['verify_result_raw'] if state['verify_result_raw']<0x80000000 else state['verify_result_raw']-0x100000000
 actual=f"写入{state['writes']}次，首次Initcall={state['init_result']}，独立验收={verified}，已校验{state['verified_count']}项，失败寄存器0x{state['failed_reg']:04X}，Timer堆={state['heap']}，断言{state['checks']}项"
 if 3<=id<=19: expected='正式Initcall成功；独立回读返回PROTOCOL，准确定位第'+str(id-3)+'项；不影响正式初始化'
 elif id==31: expected='正式Initcall成功；独立回读返回IO；不影响正式初始化'
 elif id in [0,32,33,38]: expected='真实启动配置成功；生产路径无17项验收读；独立回读17项通过；不创建Timer，不重复配置'
 elif id in [1,2,30,35,36]: expected='生产初始化保留并返回原错误；失败不标记应用初始化成功'
 elif id in [39,40,41]: expected='缺失设备/接口返回NOT_FOUND或NOT_SUPPORTED；不误标成功；接口恢复可重试'
 else: expected='对应SCCB、接口参数或状态断言全部成立'
'''+s[b:]
s=s.replace('编译真实BSP、Device、组件、Application及Framework。GPIO模型逐位解码SCCB并模拟寄存器；不代替真实电气及图像验收。','编译当前真实BSP、Device、组件、Application及Framework；17项回读函数和报告仅在本测试中。GPIO模型逐位解码SCCB并模拟寄存器；不代替真实电气及图像验收。')
(t/'run_tests.py').write_text(s,encoding='utf-8')
