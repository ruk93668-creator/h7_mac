"""ARMCC5 + Unicorn executable tests. Hardware/exception limits are reported separately."""
from pathlib import Path
import sys, subprocess, json, struct, argparse
ROOT=Path(__file__).resolve().parents[2]; HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
parser=argparse.ArgumentParser();parser.add_argument('name');parser.add_argument('--count',type=int,required=True)
args=parser.parse_args();commands=[]
def run(cmd):
    p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True)
    commands.append({'command':subprocess.list2cmdline(list(map(str,cmd))),'exit':p.returncode,'output':(p.stdout+p.stderr).decode('utf-8',errors='replace')})
    (HERE/(args.name+'_commands.json')).write_text(json.dumps(commands,indent=2),encoding='utf-8')
    if p.returncode:raise RuntimeError(commands[-1]['output'])
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'])
cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',ROOT/'CM7/BSP/bsp_dcmi_hal_config.h']
for inc in ['CM7/Core/Inc','Board/Inc','Drivers/STM32H7xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','CM7/Application','CM7/Application/Tasks','Common/Inc','CM7/Media/Inc','CM7/BSP','CM7/Platform','CM7/Service','CM7/Framework','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
    cc+=['-I',ROOT/inc]
obj=HERE/(args.name+'.o');axf=HERE/(args.name+'.axf')
objects=[]
for source in [HERE/(args.name+'.c')]+[ROOT/('Drivers/STM32H7xx_HAL_Driver/Src/'+x+'.c') for x in ['stm32h7xx_hal_dcmi','stm32h7xx_hal_dma','stm32h7xx_hal_dma_ex','stm32h7xx_hal_gpio']]:
    part=HERE/(args.name+'_'+source.stem+'.o');run(cc+['-c',source,'-o',part]);objects.append(part)
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objects,'--entry','Stage02_TestRun','--scatter',ROOT/'Tests/UsbDeviceStage1/test.sct','-o',axf])
with axf.open('rb') as f:
    elf=ELFFile(f);entry=elf.header['e_entry'];sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
rows=[]
for case in range(args.count):
    u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0xD0000000,0x20000),(0x01000000,0x1000),(0x40000000,0x100000),(0x48020000,0x10000),(0x58000000,0x40000),(0xE000E000,0x2000)]:u.mem_map(a,n)
    for a,d in sections:u.mem_write(a,d)
    u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
    error=None
    try:u.emu_start(entry|1,0x01000000,count=3000000)
    except Exception as e:error=str(e)
    line=u.reg_read(UC_ARM_REG_R0);passed=not error and line==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
    rows.append({'id':case,'pass':passed,'failure_line':line,'error':error});print(args.name,case,'PASS' if passed else 'FAIL',line,error,flush=True)
summary={'total':len(rows),'passed':sum(x['pass'] for x in rows),'failed':sum(not x['pass'] for x in rows)}
(HERE/(args.name+'_results.json')).write_text(json.dumps({'summary':summary,'cases':rows,'hardware_verified':False},indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
