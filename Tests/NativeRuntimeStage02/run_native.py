"""ARMCC5 + Unicorn executable tests. Hardware/exception limits are reported separately."""
from pathlib import Path
import sys, subprocess, json, struct, argparse
ROOT=Path(__file__).resolve().parents[2]; HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_BASEPRI
from elftools.elf.elffile import ELFFile
parser=argparse.ArgumentParser();parser.add_argument('name');parser.add_argument('--count',type=int,required=True)
args=parser.parse_args();commands=[]
def run(cmd):
    p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True)
    commands.append({'command':subprocess.list2cmdline(list(map(str,cmd))),'exit':p.returncode,'output':(p.stdout+p.stderr).decode('utf-8',errors='replace')})
    (HERE/(args.name+'_commands.json')).write_text(json.dumps(commands,indent=2),encoding='utf-8')
    if p.returncode:raise RuntimeError(commands[-1]['output'])
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',ROOT/'CM7/BSP/bsp_usb_hal_config.h']
for inc in ['CM7/Core/Inc','CM7/Device/Inc','Board','Board/Inc','Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','CM7/Application','CM7/Application/Tasks','Common/Inc','CM7/Media/Inc','CM7/BSP','CM7/Platform','CM7/Service','CM7/Framework','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
    cc+=['-I',ROOT/inc]
obj=HERE/(args.name+'.o');axf=HERE/(args.name+'.axf')
objects=[]
for source in [HERE/(args.name+'.c'),ROOT/'CM7/Service/usb_uvc_desc.c',ROOT/'CM7/Service/usb_uvc_payload.c']+list((ROOT/'Middlewares/ST/STM32_USB_Device_Library/Core/Src').glob('*.c')):
    part=HERE/(args.name+'_'+source.stem+'.o');run(cc+['-c',source,'-o',part]);objects.append(part)
for rel in ['list.c','queue.c','portable/RVDS/ARM_CM7/r0p1/port.c']:
    source=ROOT/'Middlewares/Third_Party/FreeRTOS/Source'/rel;part=HERE/(args.name+'_'+source.stem+'.o');run(cc+['-c',source,'-o',part]);objects.append(part)
import re
pcd=ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c'
exports=re.findall(r'(?m)^(?:__weak\s+)?[\w *]+\b(HAL_PCD_\w+)\([^;{}]*\)\s*\{',pcd.read_text())
renames=['-D'+name+'=Stream_'+name for name in sorted(set(exports)) if not name.endswith('Callback') and name not in ['HAL_PCD_MspInit','HAL_PCD_MspDeInit']]
for source,extra in [(pcd,renames),(ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c',[])]:
    part=HERE/(args.name+'_'+source.stem+'.o');run(cc+extra+['-c',source,'-o',part]);objects.append(part)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objects,'--entry','Stage02_TestRun','--scatter',HERE/'native.sct','--keep=*(.device_export)','--no_scanlib','-o',axf])
with axf.open('rb') as f:
    elf=ELFFile(f);entry=elf.header['e_entry'];symbols={x.name:x['st_value'] for x in elf.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
rows=[]
for case in range(args.count):
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x20000),(0x01000000,0x1000),(0x40000000,0x100000),(0x58000000,0x40000),(0xE000E000,0x2000)]:u.mem_map(a,n)
    for a,d in sections:u.mem_write(a,d)
    u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
    u.mem_write(0xE000ED0C,struct.pack('<I',3<<8))
    task_creates=[];queue_creates=[];assertions=[]
    def hook(cpu,address,size,_):
        if address==(symbols['xTaskCreateStatic']&~1):
            task_creates.append(True)
            if 20<=case<=22 and len(task_creates)==case-19:
                cpu.reg_write(UC_ARM_REG_R0,0);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        if address==(symbols['xQueueGenericCreateStatic']&~1):
            queue_creates.append(True)
            if 23<=case<=27 and len(queue_creates)==case-22:
                cpu.reg_write(UC_ARM_REG_R0,0);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        if address==(symbols['xPortStartScheduler']&~1):
            cpu.mem_write(symbols['uxCriticalNesting'],struct.pack('<I',0));cpu.mem_write(symbols['ulMaxPRIGROUPValue'],struct.pack('<I',3<<8));cpu.mem_write(symbols['ucMaxSysCallPriority'],bytes([0x50]));cpu.reg_write(UC_ARM_REG_BASEPRI,0);cpu.reg_write(UC_ARM_REG_R0,1);cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
        if address==(symbols['Platform_RtosAssert']&~1):
            assertions.append(hex(cpu.reg_read(UC_ARM_REG_LR)));cpu.emu_stop()
    u.hook_add(UC_HOOK_CODE,hook)
    error=None
    try:u.emu_start(entry|1,0x01000000,count=100000000)
    except Exception as e:error=str(e)
    line=u.reg_read(UC_ARM_REG_R0);passed=not error and not assertions and line==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
    rows.append({'id':case,'pass':passed,'failure_line':line,'error':error,'assertions':assertions,'check_line':struct.unpack('<I',u.mem_read(symbols['g_result'],4))[0]})
    if args.name=='phase02d':rows[-1]['observations']=list(struct.unpack('<13I',u.mem_read(symbols['g_native_observation'],52)))
    print(args.name,case,'PASS' if passed else 'FAIL',line,error,rows[-1]['check_line'],assertions,flush=True)
summary={'total':len(rows),'passed':sum(x['pass'] for x in rows),'failed':sum(not x['pass'] for x in rows)}
(HERE/(args.name+'_results.json')).write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
