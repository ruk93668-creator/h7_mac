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
 (here/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if r.returncode:raise RuntimeError(out)
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
a=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for d in ['Tests/Ov5640Lifecycle/stubs','Tests/CameraStreamState/stubs','Common/Inc','Board/Inc','CM7/Device/Inc','CM7/Device/Components/OV5640','CM7/Media/Inc','CM7/BSP','CM7/Platform','CM7/Framework']:a+=['-I',root/d]
objs=[]
for src,name in [(here/'lifecycle_test.c','test'),(root/'CM7/Device/Components/OV5640/ov5640.c','component'),(root/'CM7/Device/Components/OV5640/ov5640_reg.c','registers')]:
 obj=here/(name+'.o');run(a+['-c',src,'-o',obj]);objs.append(obj)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objs,'--entry','Lifecycle_TestRun','--scatter',here/'test.sct','--no_scanlib','-o',here/'test.axf'])
with (here/'test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['Y8配置保持待机','RGB565配置保持待机','YUV422配置保持待机','Device正常启停','未配置拒绝启动','RUNNING拒绝重配置','非法配置及BUSY拒绝','错误ID拒绝配置','配置IO失败后重试','启动IO错误由框架进入ERROR','停止IO错误不虚假READY','Stream启停顺序','Snapshot启停顺序','重复Stream不重复启动传感器','交叉启动不停止现有采集','Snapshot持有帧阻止停止','Stream IN_USE阻止停止','故障DRAINING等待归还','DCMI停止失败后重试','Media启动失败安全回滚','回滚停止失败优先报告','Media故障自动安全停止传感器','DRAINING释放后自动停止传感器','配置总线初始化失败不启动','传感器启动失败不启动DMA','传感器停止失败保留ERROR','未初始化Stop拒绝','ID读取IO错误','待机写入失败不能标记配置成功','停止后重启不重复配置','Snapshot释放后自动停止传感器','Start及Stop回调重入返回BUSY']
rows=[]
for i,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x20000),(0x01000000,0x1000)]:u.mem_map(addr,size)
 for addr,data in sections:u.mem_write(addr,data)
 u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,i)
 err=None
 try:u.emu_start(entry|1,0x01000000,count=50000000)
 except Exception as ex:err=str(ex)
 result=u.reg_read(UC_ARM_REG_R0);unsafe_stops=struct.unpack('<I',u.mem_read(symbols['g_unsafe_sensor_stops'],4))[0];early_dma=struct.unpack('<I',u.mem_read(symbols['g_dma_before_sensor'],4))[0];passed=not err and result==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000 and unsafe_stops==0 and early_dma==0
 rows.append({'id':i,'name':name,'result':'PASS' if passed else 'FAIL','failure_line':result,'emulator_error':err,'unsafe_sensor_stops':unsafe_stops,'dma_before_sensor':early_dma})
 print(i,name,rows[-1]['result'],result,err,flush=True)
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows)}
(here/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False},ensure_ascii=False,indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
