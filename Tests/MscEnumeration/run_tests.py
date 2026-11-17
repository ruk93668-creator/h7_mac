from pathlib import Path
import sys, subprocess, json, struct
ROOT=Path(__file__).resolve().parents[2]; OUT=ROOT/'output/MSC1/unit'; OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'output/msc1-deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0
from elftools.elf.elffile import ELFFile
TOOL=Path('D:/32/ARM/ARMCC/Bin')
args=[str(TOOL/'armcc.exe'),'--cpu','Cortex-M7','--c99','-O0','-DSTM32H747xx','-DCORE_CM7']
for inc in ['CM7/Service','CM7/Platform','CM7/Framework','Common/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
 args+=['-I',str(ROOT/inc)]
commands=[]
def run(cmd):
 r=subprocess.run(list(map(str,cmd)),capture_output=True,cwd=ROOT); text=(r.stdout+r.stderr).decode(errors='replace'); commands.append(dict(command=list(map(str,cmd)),exit=r.returncode,output=text))
 if r.returncode or 'warning' in text.lower(): raise RuntimeError(text)
objects=[]
for src in ['Tests/MscEnumeration/class_test.c','CM7/Service/usb_msc_class.c','CM7/Service/usb_msc_desc.c']:
 obj=OUT/(Path(src).stem+'.o');run(args+['-c',ROOT/src,'-o',obj]);objects.append(obj)
sct=OUT/'test.sct';sct.write_text('LR_TEST 0x08000000 0x100000 { ER_TEST 0x08000000 { .ANY (+RO) } RW_TEST 0x20000000 0x40000 { .ANY (+RW +ZI) } }')
image=OUT/'test.axf';run([TOOL/'armlink.exe',*objects,'--entry','Test_Run','--keep=Test_Checks','--scatter',sct,'-o',image])
cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000)]:cpu.mem_map(addr,size)
with image.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 for s in elf.iter_sections():
  if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']:cpu.mem_write(s['sh_addr'],s.data())
def invoke(name):
 cpu.reg_write(UC_ARM_REG_SP,0x2003fff0);cpu.reg_write(UC_ARM_REG_LR,0x01000001);cpu.emu_start(symbols[name]|1,0x01000000,count=1000000);return cpu.reg_read(UC_ARM_REG_R0)
result=invoke('Test_Run');checks=invoke('Test_Checks');report=dict(passed=result==0,failed_line=result,checks=checks,commands=commands)
(ROOT/'Tests/MscEnumeration/results.json').write_text(json.dumps(report,indent=2));print(report['passed'],checks,result);assert result==0
