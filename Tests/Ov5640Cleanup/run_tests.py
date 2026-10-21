import argparse,json,struct,subprocess,sys
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parent.parent
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
names=['Device主动读取0x5640连续10次','写地址NACK连续10次','错误ID 0x1234','前2次失败后8次成功','SDA拉低总线忙','PLL1Q错误时拒绝解除复位','DWT停滞拒绝初始化','Device参数检查','寄存器高字节NACK','寄存器低字节NACK','读地址NACK','正式运行20秒无Timer无自动ID读取','XCLK/复位/10+20ms顺序','初始化前SCCB拒绝','DWT计数回绕仍可读取','空Application初始化成功且不分配资源','SCL拉低总线忙']
commands=[]
def run(cmd):
 p=subprocess.run([str(x) for x in cmd],cwd=root,capture_output=True);text=(p.stdout+p.stderr).decode('utf-8',errors='replace')
 commands.append({'command':subprocess.list2cmdline([str(x) for x in cmd]),'code':p.returncode,'output':text})
 (here/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 print(commands[-1]['command']);print(text,end='')
 if p.returncode:raise RuntimeError('Build failed')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for inc in ['Tests/Ov5640Cleanup/stubs','CM7/BSP','CM7/Device/Inc','CM7/Application','CM7/Framework','CM7/Platform','Common/Inc','Board/Inc']:args+=['-I',root/inc]
run(args+['-c',here/'cleanup_test.c','-o',here/'cleanup_test.o'])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',here/'cleanup_test.o','--entry','Camera_TestRun','--scatter',here/'test.sct','--keep=*(.framework_init)','--keep=*(.device_export)','--no_scanlib','-o',here/'cleanup_test.axf'])
with (here/'cleanup_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
fields=['fail_line','checks','tx_count','starts','stops','nacks','reads','reset_high','mco','delays','count','passed','failed','done','heap','last_bus_error','init_result']
rows=[]
for id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.mem_map(0x08000000,0x100000);u.mem_map(0x20000000,0x40000);u.mem_map(0x01000000,0x1000)
 u.mem_map(0xE0000000,0x10000);u.mem_map(0x58024000,0x1000)
 for a,d in sections:u.mem_write(a,d)
 u.mem_write(0x58024400,struct.pack('<I',1))
 u.mem_write(0xE0001004,struct.pack('<I',0xFFFFFFF0 if id==14 else 0))
 def counter(uc,access,addr,size,value,data):
  if id!=6 and struct.unpack('<I',uc.mem_read(0xE0001000,4))[0]&1:
   n=struct.unpack('<I',uc.mem_read(0xE0001004,4))[0]
   uc.mem_write(0xE0001004,struct.pack('<I',(n+4800)&0xFFFFFFFF))
 u.hook_add(UC_HOOK_MEM_READ,counter,begin=0xE0001004,end=0xE0001007)
 u.reg_write(UC_ARM_REG_SP,0x2003FFF0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 error=None
 try:u.emu_start(entry|1,0x01000000,count=20000000,timeout=10000000)
 except Exception as exc:error=str(exc)
 state=dict(zip(fields,struct.unpack('<16Ii',u.mem_read(syms['g_result'],68))))
 passed=error is None and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not state['fail_line']
 actual=f"次数{state['count']}，成功{state['passed']}，失败{state['failed']}，done={state['done']}，Timer堆={state['heap']}，发送字节{state['tx_count']}，断言{state['checks']}"
 rows.append({'name':name,'result':'PASS' if passed else 'FAIL','actual':actual,'state':state,'error':error,'sccb_tx':bytes(u.mem_read(syms['g_wire_tx'],min(128,state['tx_count']))).hex(' ')})
 print(f"{id+1:02d} {rows[-1]['result']}: {name}: {actual}; line={state['fail_line']}; error={error}")
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows),'assertions':sum(r['state']['checks'] for r in rows)}
(here/'results.json').write_text(json.dumps({'summary':summary,'cases':rows},ensure_ascii=False,indent=2),encoding='utf-8')
lines=['# OV5640最小迁移模拟测试','','当前真实BSP/Device和清理后Application；测试主动调用Device Ops，不在正式工程恢复周期探测。GPIO模型逐位解码软件SCCB。','', '| 测试 | 实际结果 | 判定 |','|---|---|---|']
lines += [f"| {r['name']} | {r['actual']} | {r['result']} |" for r in rows]
lines += ['',str(summary),'','仅为模拟。未连接或测量真实摄像头供电、XCLK及芯片ID。GPIO模型不模拟模拟电气边沿、外部上拉和时钟延展。']
(here/'results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print('TOTAL',summary)
raise SystemExit(0 if not summary['failed'] else 1)
