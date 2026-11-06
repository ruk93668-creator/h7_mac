"""Execute production Device/OV5640/App/Media with mock board registers and DMA."""
import json, subprocess, sys, struct
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parents[1]
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
def run(args):
 r=subprocess.run(list(map(str,args)),cwd=root,capture_output=True)
 out=(r.stdout+r.stderr).decode(errors='replace');commands.append({'command':subprocess.list2cmdline(list(map(str,args))),'exit':r.returncode,'output':out})
 (here/'app_commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if r.returncode:raise RuntimeError(out)
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
a=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for d in ['Tests/Ov5640Lifecycle/stubs','Tests/CameraStreamState/stubs','Common/Inc','Board/Inc','CM7/Device/Inc','CM7/Device/Components/OV5640','CM7/Media/Inc','CM7/BSP','CM7/Platform','CM7/Framework']:a+=['-I',root/d]
objs=[]
for src,name in [(here/'app_test.c','test'),(root/'CM7/Device/Components/OV5640/ov5640.c','component'),(root/'CM7/Device/Components/OV5640/ov5640_reg.c','registers')]:
 obj=here/(name+'.o');run(a+['-c',src,'-o',obj]);objs.append(obj)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objs,'--entry','Lifecycle_TestRun','--scatter',root/'Tests/Ov5640Lifecycle/test.sct','--no_scanlib','-o',here/'test.axf'])
with (here/'test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['非Camera所有者拒绝','配置失败不启动DMA','只启动一次','READY保持直到导出','导出后释放停止','传感器停止失败不伪造成功','重复轮询不覆盖导出帧','超时保留原错误']
rows=[]
for i,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x400000),(0x01000000,0x1000)]:u.mem_map(addr,size)
 for addr,data in sections:u.mem_write(addr,data)
 u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,i)
 err=None
 try:u.emu_start(entry|1,0x01000000,count=50000000)
 except Exception as ex:err=str(ex)
 result=u.reg_read(UC_ARM_REG_R0);unsafe_stops=struct.unpack('<I',u.mem_read(symbols['g_unsafe_sensor_stops'],4))[0];early_dma=struct.unpack('<I',u.mem_read(symbols['g_dma_before_sensor'],4))[0];passed=not err and result==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000 and unsafe_stops==0 and early_dma==0
 rows.append({'id':i,'name':name,'result':'PASS' if passed else 'FAIL','failure_line':result,'emulator_error':err,'unsafe_sensor_stops':unsafe_stops,'dma_before_sensor':early_dma})
 print(i,name,rows[-1]['result'],result,err,flush=True)
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows)}
(here/'app_results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False},ensure_ascii=False,indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
