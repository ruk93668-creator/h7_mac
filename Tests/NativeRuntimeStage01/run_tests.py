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
 (HERE/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if r.returncode:raise RuntimeError(out)
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
a=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER']
for inc in ['CM7/Core/Inc','CM7/Application','CM7/Application/Tasks','CM7/Platform','CM7/Service','CM7/Framework','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1']:a+=['-I',ROOT/inc]
objs=[]
for src in [HERE/'native_test.c',ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c']+[ROOT/'Middlewares/Third_Party/FreeRTOS/Source'/s for s in ['list.c','queue.c','portable/RVDS/ARM_CM7/r0p1/port.c']]:
 obj=HERE/(src.stem+'.o');run(a+['-c',src,'-o',obj]);objs.append(obj)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objs,'--entry','Native_TestRun','--scatter',HERE/'test.sct','--no_scanlib','-o',HERE/'native_test.axf'])
with (HERE/'native_test.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['unbound and NULL port','Camera create failure','Usb create failure','Control create failure','three static tasks','duplicate creation rejected','preboot hints coalesced','init failure latches boot closed','missing task rejects prepare','scheduler without tasks rejected','ISR cannot create tasks','masked creation rejected','event parameters and BOOT forgery','task/ISR API distinction','USB callback binding failure','backend and target cannot rebind','static Idle storage','native scheduler starts four tasks','no init result no BOOT','wrong owner cannot release BOOT','BOOT gates USB Start','preboot hint survives BOOT','wait owner and output validation','real kernel wait and notification wake','real kernel timeout wake','notification before wait not lost','FromISR rejects thread caller','priority 4 IRQ rejected','preboot priority 6 IRQ deferred','runtime ISR notification','no BOOT no USB Start','BOOT starts USB once','USB Start failure posts fault','UVC not enabled in stage01','timed wake advances USB mode','Initcall prepares only','unsupported boot mode rejected','masked task cannot block','all targets receive BOOT once','task-owned stack sizes','HAL TIM6 separate from kernel','real Port SysTick','no late task creation','USB thread event route','USB ISR event route','init result only recorded once']+[f'Initcall level {i} failure stops later levels' for i in range(6)]+['USB mode timeout reported once','Camera actual entry blocks forever','Usb actual entry blocks 20ms','Control actual entry releases BOOT and blocks 1000ms']
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_BASEPRI
rows=[]
for case_id,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40000000,0x100000)]:u.mem_map(addr,size)
 for addr,data in sections:u.mem_write(addr,data)
 u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case_id)
 u.mem_write(0xE000ED0C,struct.pack('<I',3<<8))
 creates=[];assertions=[];waits=[];task_loop_stop=[];task_entered=[]
 def hook(cpu,address,size,_):
  if case_id>=53 and address==(symbols[{53:'CameraTask_Run',54:'UsbTask_Run',55:'ControlTask_Run'}[case_id]]&~1):task_entered.append(True)
  if case_id>=53 and task_entered and address==(symbols['App_EventsWait']&~1):
   waits.append(cpu.reg_read(UC_ARM_REG_R1))
   if len(waits)==2:
    task_loop_stop.append(True);cpu.emu_stop();return
  if address==(symbols['xTaskCreateStatic']&~1):
   creates.append(True)
   if 1<=case_id<=3 and len(creates)==case_id:
    cpu.reg_write(UC_ARM_REG_R0,0);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  if address==(symbols['xPortStartScheduler']&~1):
   cpu.mem_write(symbols['uxCriticalNesting'],struct.pack('<I',0));cpu.mem_write(symbols['ulMaxPRIGROUPValue'],struct.pack('<I',3<<8));cpu.mem_write(symbols['ucMaxSysCallPriority'],bytes([0x50]));cpu.reg_write(UC_ARM_REG_BASEPRI,0);cpu.reg_write(UC_ARM_REG_R0,1);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  if address==(symbols['Platform_RtosAssert']&~1):assertions.append(hex(cpu.reg_read(UC_ARM_REG_LR)));cpu.emu_stop()
 u.hook_add(UC_HOOK_CODE,hook)
 err=None
 try:u.emu_start(entry|1,0x01000000,count=10000000,timeout=10000000)
 except Exception as ex:err=str(ex)
 failed_line=struct.unpack('<I',u.mem_read(symbols['g_fail_line'],4))[0];checks=struct.unpack('<I',u.mem_read(symbols['g_checks'],4))[0]
 passed=not err and not assertions and not failed_line and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0
 if case_id>=53:
  expected_wait={53:0xffffffff,54:20,55:1000}[case_id]
  passed=not err and not assertions and not failed_line and bool(task_loop_stop) and waits[-1]==expected_wait
  debug_name={53:'g_camera_task_debug',54:'g_usb_task_debug',55:'g_control_task_debug'}[case_id]
  passed=passed and struct.unpack('<I',u.mem_read(symbols[debug_name],4))[0]==1
 rows.append({'id':case_id,'name':name,'result':'PASS' if passed else 'FAIL','checks':checks,'waits':waits,'failed_line':failed_line,'asserts':assertions,'error':err});print(case_id,rows[-1]['result'],name,failed_line,assertions,err,flush=True)
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows)}
(HERE/'results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False,'limits':'First SVC/exception scheduling is bypassed; manually switch real kernel contexts. Blocked-wait fixture restores TCB WAITING notification state after premature API return without PendSV; real kernel blocking lists and notify code execute. Application ISR detection is controlled by test context value; no claim of real NVIC preemption.'},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
