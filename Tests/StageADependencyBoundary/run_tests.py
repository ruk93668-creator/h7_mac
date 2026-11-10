"""ARMCC5真实代码专项；Mock不代表实板中断或总线验证。"""
from pathlib import Path
import os,sys,subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=Path(os.environ.get('STAGE_A_OUT',str(Path(os.environ['TEMP'])/'h7_stageA_48afa27')));OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(OUT/'deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
CC=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');commands=[];rows=[]
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True);t=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(command=list(map(str,cmd)),exit=p.returncode,output=t))
 if p.returncode:raise RuntimeError(t)
for source,entry_name,count in [('boundary_test.c','Boundary_Test',15),('events_test.c','Events_Test',5)]:
 for opt in ['O0','O2']:
  obj=OUT/(entry_name+'_'+opt+'.o');axf=OUT/(entry_name+'_'+opt+'.axf')
  args=[CC,'--cpu','Cortex-M7','--c99','--split_sections','-'+opt]
  for inc in [HERE/'stubs',ROOT/'CM7/Platform',ROOT/'CM7/Media/Inc',ROOT/'CM7/BSP',ROOT/'Common/Inc',ROOT/'CM7/Application',ROOT/'Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:args+=['-I',inc]
  run(args+['-c',HERE/source,'-o',obj])
  run([CC.with_name('armlink.exe'),obj,'--entry',entry_name,'--scatter',HERE/'test.sct','-o',axf])
  with axf.open('rb') as f:
   e=ELFFile(f);entry=e.header['e_entry'];sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
  for case in range(count):
   u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
   for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x800000),(0x01000000,0x1000)]:u.mem_map(a,n)
   for a,b in sections:u.mem_write(a,b)
   u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
   u.emu_start(entry|1,0x01000000,count=3000000)
   value=u.reg_read(UC_ARM_REG_R0);rows.append(dict(suite=entry_name,optimization=opt,case=case,passed=value==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000,failure_line=value))
# 对现有Application作逆替换，证明没有顺手改动业务表达式和控制流
mapping={'Platform_SyncGetException()':'__get_IPSR()','Platform_SyncGetPrimask()':'__get_PRIMASK()','Platform_SyncGetBasepri()':'__get_BASEPRI()','Platform_SyncDisableIrq()':'__disable_irq()','Platform_SyncRestoreIrq(':'__set_PRIMASK(','Platform_SyncMemoryBarrier()':'__DMB()','Platform_SyncGetIrqPriority(ipsr - 16U)':'NVIC_GetPriority((IRQn_Type)(ipsr - 16U))'}
def base(p):return subprocess.check_output(['git','-c','safe.directory='+str(ROOT),'show','48afa27:'+p],cwd=ROOT)
def decode(b):
 try:return b.decode('utf-8').replace('\r\n','\n')
 except:return b.decode('gbk').replace('\r\n','\n')
paths=list((ROOT/'CM7/Application/Tasks').glob('*.c'))+[ROOT/'CM7/Application/app_tasks.c',ROOT/'CM7/Application/app_messages.c',ROOT/'CM7/Application/app_storage.c']
for p in paths:
 rel=p.relative_to(ROOT).as_posix();s=decode(p.read_bytes());old=decode(base(rel))
 for a,b in mapping.items():s=s.replace(a,b)
 s=s.replace('#include "platform_sync.h"','#include "stm32h7xx.h"')
 if p.name=='app_storage.c':
  a=old.index('typedef char StorageIndexBounds');a=old.rfind('//',0,a);b=old.index('\n\n',old.index('? 1 : -1];',a))+2;old=old[:a]+old[b:];old=old.replace('#include "bsp_sdram.h"\n','')
 rows.append(dict(case=rel,passed=s==old))
# 用真实CMSIS和平台目标文件验证MRS/MSR/CPSID，不只依赖CPU替身
from unicorn.arm_const import UC_ARM_REG_PRIMASK,UC_ARM_REG_BASEPRI,UC_ARM_REG_IPSR
for opt in ['O0','O2']:
 args=[CC,'--cpu','Cortex-M7','--c99','--split_sections','-'+opt,'-DCORE_CM7','-DSTM32H747xx']
 for inc in ['CM7/Platform','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include']:args+=['-I',ROOT/inc]
 objects=[]
 for name,source in [('cpu',HERE/'cpu_test.c'),('sync',ROOT/'CM7/Platform/platform_sync.c')]:
  obj=OUT/(name+opt+'.o');run(args+['-c',source,'-o',obj]);objects.append(obj)
 axf=OUT/('cpu'+opt+'.axf');run([CC.with_name('armlink.exe'),*objects,'--entry','Cpu_Test','--scatter',HERE/'test.sct','-o',axf])
 with axf.open('rb') as f:
  e=ELFFile(f);entry=e.header['e_entry'];sections=[(v['sh_addr'],v.data()) for v in e.iter_sections() if v['sh_flags']&2 and v['sh_type']=='SHT_PROGBITS' and v['sh_size']]
 for initial in [0,1]:
  u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0xE000E000,0x2000),(0x01000000,0x1000)]:u.mem_map(addr,size)
  for addr,b in sections:u.mem_write(addr,b)
  u.mem_write(0xE000E409,bytes([0x70]));u.reg_write(UC_ARM_REG_PRIMASK,initial);u.reg_write(UC_ARM_REG_BASEPRI,0x50);u.reg_write(UC_ARM_REG_IPSR,37)
  u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,initial);u.emu_start(entry|1,0x01000000,count=10000)
  line=u.reg_read(UC_ARM_REG_R0);rows.append(dict(suite='real_CMSIS',optimization=opt,case=initial,passed=line==0,failure_line=line))
# 被迁移的断言必须仍然拦截索引重叠和越界配置
for name,definition in [('overlap','MJPEG_AVI_INDEX_BASE CAMERA_MJPEG_FIFO_BASE'),('outside','BSP_SDRAM_END MJPEG_AVI_INDEX_BASE')]:
 source=OUT/('bounds_'+name+'.c');macro=definition.split()[0]
 source.write_text('#include "bsp_sdram.h"\n#include "camera_mjpeg_fifo.h"\n#include "mjpeg_avi.h"\n#undef '+macro+'\n#define '+definition+'\n#include "'+(ROOT/'CM7/Service/mjpeg_avi.c').as_posix()+'"\n')
 args=[CC,'--cpu','Cortex-M7','--c99','-c',source,'-o',OUT/('bounds_'+name+'.o')]
 for inc in ['CM7/Service','CM7/Platform','CM7/BSP','CM7/Media/Inc','Common/Inc']:args+=['-I',ROOT/inc]
 p=subprocess.run(list(map(str,args)),capture_output=True);log=(p.stdout+p.stderr).decode(errors='replace')
 rows.append(dict(case='range_reject_'+name,passed=p.returncode!=0 and 'StorageIndexBounds' in log));commands.append(dict(command=list(map(str,args)),exit=p.returncode,expected_compile_failure=True,output=log))
# App_Events除边界替换和删除原BSP绑定外保持原表达式不变
path='CM7/Application/app_events.c';old=decode(base(path));now=decode((ROOT/path).read_bytes())
for a,b in mapping.items():now=now.replace(a,b)
now=now.replace('#include "platform_sync.h"','#include "stm32h7xx.h"')
old=old.replace('#include "bsp_dcmi.h"\n','');block='    result = BSP_DCMI_SetEventCallback(AppEvents_CameraNotify, NULL);\n    if (result != SYS_OK)\n        return result;\n';old=old.replace(block,'',1)
rows.append(dict(case='App_Events_inverse_substitution',passed=old==now))
result=dict(summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),failed=sum(not x['passed'] for x in rows)),cases=rows,hardware_verified=False)
(OUT/'boundary_results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');(OUT/'boundary_commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8');print(json.dumps(result,indent=2));raise SystemExit(result['summary']['failed']!=0)
