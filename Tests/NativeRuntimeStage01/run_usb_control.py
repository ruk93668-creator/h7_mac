"""Native UsbTask plus real Device Core/Mode/PCD BSP and unchanged HAL mocks."""
from pathlib import Path
import sys,json,subprocess,struct
sys.stdout.reconfigure(encoding='utf-8')
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_BASEPRI
from elftools.elf.elffile import ELFFile
commands=[]
def run(args):
 r=subprocess.run(list(map(str,args)),cwd=ROOT,capture_output=True);out=(r.stdout+r.stderr).decode('utf-8',errors='replace');commands.append({'command':subprocess.list2cmdline(list(map(str,args))),'exit':r.returncode,'output':out});(HERE/'usb_commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if r.returncode:raise RuntimeError(out)
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
a=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',ROOT/'CM7/BSP/bsp_usb_hal_config.h']
for inc in ['CM7/Core/Inc','CM7/Application','CM7/Application/Tasks','CM7/BSP','CM7/Platform','CM7/Service','CM7/Framework','Common/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1']:a+=['-I',ROOT/inc]
objs=[]
sources=[HERE/'usb_control_test.c',ROOT/'CM7/Application/app_events.c']+list((ROOT/'Middlewares/ST/STM32_USB_Device_Library/Core/Src').glob('*.c'))+[ROOT/'Middlewares/Third_Party/FreeRTOS/Source'/f for f in ['tasks.c','list.c','queue.c','portable/RVDS/ARM_CM7/r0p1/port.c']]
for src in sources:
 obj=HERE/('usb_'+src.stem+'.o');run(a+['-c',src,'-o',obj]);objs.append(obj)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objs,'--entry','NativeUsb_TestRun','--scatter',HERE/'test.sct','--keep=*(.framework_init)','--no_scanlib','-o',HERE/'usb_control.axf'])
with (HERE/'usb_control.axf').open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry'];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
names=['Initcall keeps TEST disconnected','BOOT starts TEST once','GET_DESCRIPTOR Device','SET_ADDRESS status','SET_CONFIGURATION RUNNING','duplicate BOOT no duplicate Start','enumeration timeout','Reset posts wake and reopens EP0','Disconnect posts wake','Start HAL_BUSY propagated','Initcall PCD failure keeps BOOT closed','STALL then legal SETUP','repeated Reset','cannot replace live notification callback','unsupported TEST interface STALL then legal SETUP']
rows=[]
for i,name in enumerate(names):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0x58020000,0x10000),(0xE000E000,0x2000),(0x40000000,0x100000)]:u.mem_map(addr,size)
 for addr,data in sections:u.mem_write(addr,data)
 u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,i);u.mem_write(0xE000ED0C,struct.pack('<I',3<<8))
 asserts=[]
 def hook(cpu,address,size,_):
  if address==(symbols['xPortStartScheduler']&~1):
   cpu.mem_write(symbols['uxCriticalNesting'],struct.pack('<I',0));cpu.mem_write(symbols['ulMaxPRIGROUPValue'],struct.pack('<I',3<<8));cpu.mem_write(symbols['ucMaxSysCallPriority'],bytes([0x50]));cpu.reg_write(UC_ARM_REG_BASEPRI,0);cpu.reg_write(UC_ARM_REG_R0,1);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
  if address==(symbols['Platform_RtosAssert']&~1):asserts.append(hex(cpu.reg_read(UC_ARM_REG_LR)));cpu.emu_stop()
 u.hook_add(UC_HOOK_CODE,hook);err=None
 try:u.emu_start(entry|1,0x01000000,count=10000000,timeout=10000000)
 except Exception as ex:err=str(ex)
 line=struct.unpack('<I',u.mem_read(symbols['g_result'],4))[0];passed=not err and not asserts and not line and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0
 rows.append({'id':i,'name':name,'result':'PASS' if passed else 'FAIL','failed_line':line,'observed':struct.unpack('<6I',u.mem_read(symbols['g_result'],24)),'asserts':asserts,'error':err});print(i,rows[-1]['result'],name,line,rows[-1].get('observed'),asserts,err,flush=True)
summary={'total':len(rows),'passed':sum(r['result']=='PASS' for r in rows),'failed':sum(r['result']=='FAIL' for r in rows)}
(HERE/'usb_results.json').write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False,'limits':'Initial SVC is bypassed; real kernel and USB software paths execute with controlled HAL hardware, not physical host enumeration.'},indent=2),encoding='utf-8');print(summary);raise SystemExit(bool(summary['failed']))
