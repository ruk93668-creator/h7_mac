"""Real FreeRTOS kernel and RVDS port; scheduler start exception is simulated explicitly."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "Tests/OledAsyncAcceptance/python_deps"))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile


commands=[]
def run(args):
 r=subprocess.run(list(map(str,args)),cwd=ROOT,capture_output=True)
 out=(r.stdout+r.stderr).decode('utf-8',errors='replace')
 commands.append({'command':subprocess.list2cmdline(list(map(str,args))),'exit':r.returncode,'output':out})
 (HERE/'task_commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if r.returncode:raise RuntimeError(out)
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
a=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER']
for inc in ['CM7/Core/Inc','CM7/Application','CM7/Application/Tasks','CM7/Platform','CM7/Service','CM7/Framework','CM7/Media/Inc','CM7/BSP','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1']:a+=['-I',ROOT/inc]
objs=[]
for src in [HERE/'kernel_test.c',ROOT/'Common/Src/ipc_application.c',ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c']+[ROOT/'Middlewares/Third_Party/FreeRTOS/Source'/s for s in ['list.c','queue.c','portable/RVDS/ARM_CM7/r0p1/port.c']]:
 obj=ROOT/'output/IpcStatus'/('task_'+src.stem+'.o');run(a+['-c',src,'-o',obj]);objs.append(obj)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objs,'--entry','IpcTaskStatus_TestRun','--scatter',HERE/'task_test.sct','--no_scanlib','-o',ROOT/'output/IpcStatus/task_test.axf'])
with (ROOT/'output/IpcStatus/task_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]

from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_INVALID, UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_BASEPRI
names=['five BOOT targets','ISR create rejected','duplicate IPC create','IPC static create failure','owner registration failure','IPC attach failure','init failure closes BOOT','missing IPC attachment','pending IPC event survives BOOT','IPC cannot release BOOT','IPC entry waits before BOOT','duplicate BOOT rejected','late IPC create rejected','all five receive BOOT','only IPC task is writer','static stack size/alignment','BOOT starts service and blocks 100ms','ISR not owner','wait for other task rejected','duplicate app create rejected']
names+=['initial invalid snapshot','IPC cannot impersonate camera','ISR snapshot rejected','masked snapshot rejected','shared output rejected','three real task records aggregate','USB cannot write camera','camera cannot collect IPC snapshot','per-source age and stale','frame counter wraps','invalid control state','invalid USB result','invalid camera format','output integer overflow','aggregate numbering advances','aggregate numbering wraps']
names+=['paired sample Tick','sample Tick shared alias rejected','two outputs cannot overlap','sample Tick NULL rejected','sample Tick after wrap']
rows=[]
for case_id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40000000,0x100000)]:u.mem_map(addr,size)
 for addr,data in sections:u.mem_write(addr,data)
 u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case_id)
 u.mem_write(0xE000ED0C,struct.pack('<I',3<<8))
 u.mem_write(symbols['uxCriticalNesting'],struct.pack('<I',0))
 creates=[];assertions=[];stopped=[];args=[];unsafe=[]
 def hook(cpu,address,size,_):
  if address==(symbols['xTaskCreateStatic']&~1):
   creates.append(True)
   if case_id==3 and len(creates)==4:
    cpu.reg_write(UC_ARM_REG_R0,0);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  if case_id==5 and address==(symbols['App_EventsAttach']&~1) and cpu.reg_read(UC_ARM_REG_R0)==3:
   cpu.reg_write(UC_ARM_REG_R0,0xfffffff7);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  if case_id==10 and address==(symbols['App_EventsWait']&~1):
   args.append(cpu.reg_read(UC_ARM_REG_R1));stopped.append(True);cpu.emu_stop()
  if case_id==16 and address==(symbols['vTaskDelay']&~1):
   args.append(cpu.reg_read(UC_ARM_REG_R0));stopped.append(True);cpu.emu_stop()
  if address==(symbols['Platform_RtosAssert']&~1):assertions.append(hex(cpu.reg_read(UC_ARM_REG_LR)));cpu.emu_stop()
 u.hook_add(UC_HOOK_CODE,hook)
 def badmem(cpu,access,address,size,value,data):
  print('MEMORY',case_id,hex(address),'PC',hex(cpu.reg_read(UC_ARM_REG_PC)),'LR',hex(cpu.reg_read(UC_ARM_REG_LR)),flush=True)
  return False
 u.hook_add(UC_HOOK_MEM_INVALID,badmem)
 def memory_write(cpu,access,address,size,value,data):
  for key,count in [('s_status',80),('g_app_status_debug',80),('s_source_ticks',12),('s_source_expired',3),('s_snapshot_id',4)]:
   if address < symbols[key]+count and symbols[key] < address+size:
    if cpu.reg_read(UC_ARM_REG_BASEPRI)!=0x50:unsafe.append(key)
 u.hook_add(UC_HOOK_MEM_WRITE,memory_write)
 err=None
 try:u.emu_start(entry|1,0x01000000,count=10000000,timeout=10000000)
 except Exception as ex:err=str(ex)
 def value(name):return struct.unpack('<I',u.mem_read(symbols[name],4))[0]
 fail=value('g_fail_line');starts=value('g_runtime_starts');steps=value('g_runtime_steps')
 passed=not unsafe and not err and not assertions and not fail and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0
 if case_id==10:passed=not unsafe and not err and not assertions and not fail and bool(stopped) and args==[0xffffffff] and starts==0 and steps==0
 if case_id==16:passed=not unsafe and not err and not assertions and not fail and bool(stopped) and args==[100] and starts==1 and steps==1
 rows.append(dict(id=case_id,name=name,result='PASS' if passed else 'FAIL',failed_line=fail,error=err,assertions=assertions,args=args,unsafe_writes=unsafe));print(rows[-1],flush=True)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows))
(HERE/'task_results.json').write_text(json.dumps(dict(summary=summary,cases=rows,hardware_verified=False,limits='Real kernel task creation and notifications; current task and running state selected by fixture. SVC/PendSV/NVIC preemption not emulated. IPC infinite loop stopped at wait/delay API entry.'),indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
