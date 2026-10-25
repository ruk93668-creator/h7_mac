import json,struct,subprocess,sys,hashlib
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parents[1]
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
source_before={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in ["CM7/BSP/bsp_usb.c","CM7/Platform/usb_device.c"]}
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=root,capture_output=True)
 output=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append({'command':subprocess.list2cmdline(list(map(str,cmd))),'returncode':p.returncode,'output':output})
 (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 if p.returncode:print(output);raise RuntimeError('compile/link failed')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',root/'CM7/BSP/bsp_usb_hal_config.h']
for inc in ['CM7/Core/Inc','CM7/BSP','CM7/Platform','CM7/Framework','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:args+=['-I',root/inc]
objects=[]
for source in [here/'usb_test.c']+list((root/'Middlewares/ST/STM32_USB_Device_Library/Core/Src').glob('*.c')):
 obj=here/(source.stem+'.o');run(args+['-c',source,'-o',obj]);objects.append(obj)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objects,'--entry','Stream_TestRun','--scatter',here/'test.sct','--keep=*(.framework_init)','--no_scanlib','-o',here/'stream_test.axf'])
with (here/'stream_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
groups=[('未初始化接口', '未初始化接口', '断言通过'), ('硬件时钟GPIO配置保持PLL', '硬件时钟GPIO配置保持PLL', '断言通过'), ('HSI48失败', 'HSI48失败', '断言通过'), ('USB时钟选择失败', 'USB时钟选择失败', '断言通过'), ('时钟频率错误', '时钟频率错误', '断言通过'), ('PCD初始化失败', 'PCD初始化失败', '断言通过'), ('Rx FIFO失败', 'Rx FIFO失败', '断言通过'), ('Tx FIFO失败', 'Tx FIFO失败', '断言通过'), ('重复初始化', '重复初始化', '断言通过'), ('启动与停止NVIC', '启动与停止NVIC', '断言通过'), ('启动HAL_BUSY', '启动HAL_BUSY', '断言通过'), ('Stop底层错误保留', 'Stop底层错误保留', '断言通过'), ('Reset打开EP0', 'Reset打开EP0', '断言通过'), ('完整设备描述符', '完整设备描述符', '断言通过'), ('设备描述符前8字节', '设备描述符前8字节', '断言通过'), ('配置描述符头', '配置描述符头', '断言通过'), ('完整配置描述符', '完整配置描述符', '断言通过'), ('语言描述符', '语言描述符', '断言通过'), ('厂家字符串', '厂家字符串', '断言通过'), ('产品字符串', '产品字符串', '断言通过'), ('序列号字符串', '序列号字符串', '断言通过'), ('设置地址', '设置地址', '断言通过'), ('非法地址STALL', '非法地址STALL', '断言通过'), ('未寻址禁止配置', '未寻址禁止配置', '断言通过'), ('配置完成', '配置完成', '断言通过'), ('取消配置', '取消配置', '断言通过'), ('GET_CONFIGURATION', 'GET_CONFIGURATION', '断言通过'), ('GET_STATUS', 'GET_STATUS', '断言通过'), ('未知描述符STALL', '未知描述符STALL', '断言通过'), ('厂商请求STALL', '厂商请求STALL', '断言通过'), ('UVC Probe不支持', 'UVC Probe不支持', '断言通过'), ('SET_INTERFACE视频切换不支持', 'SET_INTERFACE视频切换不支持', '断言通过'), ('GET_INTERFACE零', 'GET_INTERFACE零', '断言通过'), ('挂起与恢复', '挂起与恢复', '断言通过'), ('Reset清除配置', 'Reset清除配置', '断言通过'), ('停止重新枚举', '停止重新枚举', '断言通过'), ('DeInit关闭资源', 'DeInit关闭资源', '断言通过'), ('非法Class注册', '非法Class注册', '断言通过'), ('运行中禁止注册', '运行中禁止注册', '断言通过'), ('停止时Class注册', '停止时Class注册', '断言通过'), ('固定内存分配', '固定内存分配', '断言通过'), ('非零端点未启用', '非零端点未启用', '断言通过'), ('LL错误传播', 'LL错误传播', '断言通过'), ('其他PCD回调隔离', '其他PCD回调隔离', '断言通过'), ('IRQ调用PCD', 'IRQ调用PCD', '断言通过'), ('仅FS拒绝Qualifier', '仅FS拒绝Qualifier', '断言通过'), ('STALL清除', 'STALL清除', '断言通过')]
groups += [('EP0数据及状态阶段回调', 'EP0数据及状态阶段回调', '断言通过'), ('配置描述符长度截断', '配置描述符长度截断', '断言通过'), ('非法配置值STALL', '非法配置值STALL', '断言通过'), ('Class字符串回调完整性', 'Class字符串回调完整性', '断言通过'), ('重复Class注册数量', '重复Class注册数量', '断言通过')]
rows=[]
for kind,(name,input_,expected) in enumerate(groups):
 for fmt,label in enumerate(['FS']):
  id=kind;u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x20000),(0x01000000,0x1000),(0x58020000,0x10000)]:u.mem_map(a,n)
  for a,d in sections:u.mem_write(a,d)
  u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
  error=None
  try:u.emu_start(entry|1,0x01000000,count=1500000000,timeout=45000000)
  except Exception as exc:error=str(exc)
  state=dict(zip(['fail_line','checks','operations','replaces','deferred','case_id'],struct.unpack('<6I',u.mem_read(syms['g_result'],24))))
  passed=not error and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line']
  rows.append({'id':id,'name':name+' / '+label,'input':input_,'expected':expected,'result':'PASS' if passed else 'FAIL','actual':state,'error':error})
  print(f"{id+1:02d} {rows[-1]['result']} {name}/{label}: {state}; {error}",flush=True)
  (here/'results.json').write_text(json.dumps({'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows),'assertions':sum(r['actual']['checks'] for r in rows)}
baseline=source_before;changed=[f for f,h in baseline.items() if hashlib.sha256((root/f).read_bytes()).hexdigest()!=h]
(here/'results.json').write_text(json.dumps({'summary':summary,'formal_source_changed':changed,'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# USB Device第一阶段模拟测试','','直接编译正式BSP、Platform及ST Device Core；HAL PCD、时钟、GPIO使用可控桩。Unicorn执行ARMCC5生成的ARM机器码。','', '| 用例 | 输入 | 预期 | 实际 | 结果 |','|---|---|---|---|---|']
for row in rows:lines.append('| '+row['name']+' | '+row['input']+' | '+row['expected']+' | '+str(row['actual'])+' | '+row['result']+' |')
lines+=['',str(summary),'','正式源文件变化：'+str(changed),'','限制：不连接USB主机，不验证物理48MHz精度、USB电气信号和Windows枚举；HAL硬件函数在本套中使用桩，真实HAL仅由正式Target编译验收。']
(here/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('TOTAL',summary,'formal source changed:',changed,flush=True)
raise SystemExit(bool(summary['failed'] or changed))
