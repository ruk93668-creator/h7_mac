from pathlib import Path
import shutil
p=Path('Tests/Ov5640Y8');shutil.copytree('Tests/Ov5640Cleanup/stubs',p/'stubs',dirs_exist_ok=True);shutil.copyfile('Tests/Ov5640Cleanup/test.sct',p/'test.sct')
s=Path('Tests/Ov5640Cleanup/cleanup_test.c').read_text(encoding='utf-8')
s=s[:s.index('static void Step(')]
s=s.replace('#include "../../CM7/Application/app_main.c"','#include "../../CM7/Application/app_camera.c"\n#include "../../CM7/Application/app_main.c"')
s=s.replace('static uint32_t now,clock_q','static uint8_t registers[65536];\nstatic int32_t mismatch_reg = -1,fail_reg = -1;\nstatic uint32_t writes,verify_active,fail_read;\nstatic uint32_t now,clock_q')
s=s.replace('return now;}','return now++;}')
s=s.replace('      current_len++;','''      if(current_len==3 && packet_first==0x78 && nack_index!=3 && (int32_t)reg_pointer!=fail_reg){
       if(reg_pointer==0x3008 && acc==0x82){uint32_t j;for(j=0;j<65536;j++)registers[j]=0;}
       registers[reg_pointer]=(uint8_t)acc;writes++;
       if(reg_pointer==0x503D && acc==0x80)verify_active=1;
      }
      current_len++;''')
s=s.replace('uint32_t value=reg_pointer==0x300A?((sensor_id>>8)&255):(sensor_id&255);','uint32_t value=reg_pointer==0x300A?((sensor_id>>8)&255):(reg_pointer==0x300B?(sensor_id&255):registers[reg_pointer]);if(verify_active && (int32_t)reg_pointer==mismatch_reg)value^=1;')
s=s.replace('(int32_t)ack_stage==nack_index?GPIO_PIN_SET:GPIO_PIN_RESET','((int32_t)ack_stage==nack_index || (ack_stage==3 && packet_first==0x78 && (int32_t)reg_pointer==fail_reg) || (fail_read && verify_active && packet_first==0x79))?GPIO_PIN_SET:GPIO_PIN_RESET')
s+='''
static int Cases(uint32_t id)
{
 uint32_t i,n;
 uint8_t value=0,bytes[2]={0xA5,0x5A};
 Ov5640ConfigReport_t report;
 SysResult_t result;
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
  EQ(s_camera_ops.configure_y8_colorbar(NULL,&report),SYS_ERR_INVALID_ARG);
  EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640,NULL),SYS_ERR_INVALID_ARG);
  EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640,&report),SYS_ERR_INVALID_STATE);
  s_ov5640.base.state=DEVICE_STATE_BUSY;EQ(s_camera_ops.configure_y8_colorbar(&s_ov5640,&report),SYS_ERR_BUSY);EQ(writes,0);return 0;
 }
 if(id==37){nack_index=3;EQ(BSP_Camera_ReadReg(0x300A,&value),SYS_ERR_IO);EQ(s_sccb_error,0x20);return 0;}
 if(id==1)sensor_id=0x1234;
 if(id>=3 && id<=19)mismatch_reg=s_verify[id-3].reg;
 if(id==30 || id==32)fail_reg=0x3036;
 if(id==31)fail_read=1;
 if(id==2)nack_index=0;
 result=Framework_InitRuntime();g_result.init_result=result;
 EQ(g_camera_report.done,1);EQ(g_camera_report.result,result);EQ(s_ov5640.base.state,DEVICE_STATE_READY);
 EQ(s_heap_size,0);EQ(s_timer_pool_count,0);EQ(gpio_b_calls,1);
 if(id==1 || (id>=3 && id<=19)){
  EQ(result,SYS_ERR_PROTOCOL);EQ(g_camera_report.sensor.configured,0);
  if(id==1){EQ(writes,0);EQ(g_camera_report.sensor.chip_id,0x1234);}
  else{EQ(g_camera_report.sensor.failed_reg,s_verify[id-3].reg);EQ(g_camera_report.sensor.checked_count,id-3);}
  return 0;
 }
 if(id==2 || id==30 || id==31 || id==32){
  EQ(result,SYS_ERR_IO);EQ(g_camera_report.sensor.configured,0);
  if(id!=32)return 0;
  fail_reg=-1;EQ(App_Init(),SYS_OK);
 }
 else EQ(result,SYS_OK);
 EQ(g_camera_report.sensor.chip_id,0x5640);EQ(g_camera_report.sensor.width,160);EQ(g_camera_report.sensor.height,120);
 EQ(g_camera_report.sensor.checked_count,OV5640_VERIFY_COUNT);EQ(g_camera_report.sensor.configured,1);
 for(i=0;i<OV5640_VERIFY_COUNT;i++)EQ(registers[s_verify[i].reg],s_verify[i].value);
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
(p/'camera_test.c').write_text(s,encoding='utf-8')
# runner reuses ARMCC/Unicorn harness; compile component sources separately, real BSP via GPIO model.
s=Path('Tests/Ov5640Cleanup/run_tests.py').read_text(encoding='utf-8')
s=s.replace("here/'cleanup_test", "here/'camera_test").replace('Tests/Ov5640Cleanup/stubs','Tests/Ov5640Y8/stubs')
names=['正常Initcall配置160x120 Y8彩条','错误芯片ID禁止写表','SCCB读ID地址NACK']+[f'关键寄存器回读不一致-{i}' for i in range(17)]+['写地址NACK','寄存器高字节NACK','寄存器低字节NACK','数据字节NACK','真实SCCB写后读','初始化前禁止写','SDA忙拒绝写','SCL忙拒绝写','软件复位等待5ms','组件多字节和参数边界','组件写表故障保留IO','关键寄存器回读NACK','配置失败后重试','重复应用初始化和主循环无重复配置','Device参数和状态检查','PLL错误阻止初始化','DWT停止阻止初始化','SCCB读地址NACK','DWT回绕仍能完整配置']
a=s.index('names=');b=s.index('\ncommands=',a);s=s[:a]+'names='+repr(names)+s[b:]
s=s.replace("'CM7/Device/Inc'","'CM7/Device/Components/OV5640','CM7/Device/Inc'")
s=s.replace("run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'camera_test.o'", "for source in ['ov5640','ov5640_reg']:\n run(args+['-c',root/('CM7/Device/Components/OV5640/'+source+'.c'),'-o',here/(source+'.o')])\nrun(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'ov5640.o',here/'ov5640_reg.o',here/'camera_test.o'")
s=s.replace('if id==14 else 0','if id==38 else 0').replace('if id!=6 and','if id!=36 and').replace('count=20000000,timeout=10000000','count=100000000,timeout=30000000')
s=s.replace('当前真实BSP/Device和清理后Application；测试主动调用Device Ops，不在正式工程恢复周期探测。GPIO模型逐位解码软件SCCB。','编译真实BSP、Device、组件、Application及Framework。GPIO模型逐位解码SCCB并模拟寄存器；不代替真实电气及图像验收。')
(p/'run_tests.py').write_text(s,encoding='utf-8')
