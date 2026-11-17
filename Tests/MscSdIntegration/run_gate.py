from pathlib import Path
import sys, subprocess, json, struct
ROOT=Path(__file__).resolve().parents[2]; OUT=ROOT/'output/MSC24/sd-gate'; OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'output/msc1-deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn import UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_PC, UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_IPSR
from elftools.elf.elffile import ELFFile
TOOL=Path('D:/32/ARM/ARMCC/Bin')
args=[str(TOOL/'armcc.exe'),'--cpu','Cortex-M7','--c99','--split_sections','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','-DUSB_MODE_BOOT_MSC=1','-DUSB_MSC_SD_TEST=1',*(['-DUSB_MSC_SD_WRITE_ENABLE=1'] if '--write' in sys.argv else []),'--preinclude',str(ROOT/'CM7/BSP/bsp_usb_hal_config.h')]
for inc in ['Middlewares/Third_Party/FreeRTOS/Source/include','Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM7/r0p1','Middlewares/Third_Party/FatFs','CM7/Core/Inc','CM7/BSP','Drivers/STM32H7xx_HAL_Driver/Inc','CM7/Service','CM7/Platform','CM7/Framework','Common/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32H7xx/Include','Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:
 args+=['-I',str(ROOT/inc)]
commands=[]
def run(cmd):
 r=subprocess.run(list(map(str,cmd)),capture_output=True,cwd=ROOT); text=(r.stdout+r.stderr).decode(errors='replace'); commands.append(dict(command=list(map(str,cmd)),exit=r.returncode,output=text))
 if r.returncode or 'warning' in text.lower(): raise RuntimeError(text)
objects=[]
for src in ['Tests/MscSdIntegration/gate_test.c']:
 obj=OUT/(Path(src).stem+'.o');run(args+['-c',ROOT/src,'-o',obj]);objects.append(obj)
run([TOOL/'armasm.exe','--cpu','Cortex-M7',ROOT/'Tests/MscSdIntegration/gate_trap.s','-o',OUT/'gate_trap.o'])
objects.append(OUT/'gate_trap.o')
sct=ROOT/'Tests/MscSdIntegration/test.sct'
image=OUT/'test.axf';run([TOOL/'armlink.exe',*objects,'--entry','Gate_Run','--no_scanlib','--scatter',sct,'-o',image])
cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(0xD0000000,0x20000),(0x58020000,0x10000),(0x40080000,0x40000),(0xe0000000,0x100000)]:cpu.mem_map(addr,size)
with image.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 for s in elf.iter_sections():
  if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']:cpu.mem_write(s['sh_addr'],s.data())
def invoke(name):
 cpu.reg_write(UC_ARM_REG_SP,0x2003fff0);cpu.reg_write(UC_ARM_REG_LR,0x01000001);cpu.emu_start(symbols[name]|1,0x01000000,count=1000000);return cpu.reg_read(UC_ARM_REG_R0)

def invalid(cpu,access,address,size,value,user):
    print('MEMORY',scenario,hex(cpu.reg_read(UC_ARM_REG_PC)),hex(address),size,flush=True)
    return False
cpu.hook_add(UC_HOOK_MEM_INVALID,invalid)
rows = []
for scenario in range(1):
    # 重建全部可写内存，避免上一场景的静态状态污染下一场景
    cpu.mem_write(0x20000000, bytes(0x40000))
    with image.open('rb') as source:
        elf = ELFFile(source)
        for section in elf.iter_sections():
            if section['sh_flags'] & 2 and section['sh_type'] == 'SHT_PROGBITS' and section['sh_size']:
                cpu.mem_write(section['sh_addr'], section.data())
    cpu.reg_write(UC_ARM_REG_R0, scenario)
    result = invoke('Gate_Run')
    failure, checks = struct.unpack('<2I', cpu.mem_read(symbols['g_result'], 8))
    rows.append(dict(scenario=scenario, passed=result == 0 and failure == 0,
                     failed_line=failure, assertions=checks))
report = dict(base_commit='14f44ca678647421d3519f192d441724178a7882',
              meaning='real SD_Block/StorageFS gate with mocked RTOS and forbidden-call traps',
              cases=rows, commands=commands)
import hashlib
report['tested_parent'] = subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'rev-parse','HEAD'],cwd=ROOT).decode().strip()
source_paths = sorted((ROOT/'CM7/Service').glob('usb_msc*')) + [ROOT/name for name in ['CM7/Platform/usb_device.c','CM7/Platform/sd_block.c','CM7/Platform/storage_fs.c','CM7/Application/Tasks/storage_task.c','CM7/Application/Tasks/usb_task.c']]
report['source_sha256'] = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in source_paths}
(ROOT/'Tests/MscSdIntegration/gate_results.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
for row in rows:
    print(row)
assert all(row['passed'] for row in rows)
