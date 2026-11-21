"""Compile and execute actual Camera BSP with ARMCC5; mock only GPIO/clock hardware."""
from pathlib import Path
import argparse
import subprocess,sys,json
ROOT=Path(__file__).resolve().parents[2];HERE=ROOT/'Tests/CameraPowerV1'
parser = argparse.ArgumentParser()
parser.add_argument("output", type=Path)
parser.add_argument("--optimization", choices=("O0", "O2"), default="O0")
args = parser.parse_args()
OUT = args.output.resolve()
OUT.mkdir(parents=True, exist_ok=False)
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
logs=[]
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),capture_output=True);logs.append({'command':list(map(str,cmd)),'exit':p.returncode,'output':(p.stdout+p.stderr).decode(errors='replace')});(OUT/'commands.json').write_text(json.dumps(logs,indent=2))
 if p.returncode:raise RuntimeError(logs[-1])
obj=OUT/'test.o';axf=OUT/'test.axf';sc=OUT/'test.sct'
sc.write_text('LR_TEST 0x08000000 0x100000 { ER_TEST 0x08000000 { .ANY (+RO) } RW_TEST 0x20000000 0x40000 { .ANY (+RW +ZI) } }')
run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7.fp.sp','--c99','-' + args.optimization,'-I',HERE,'-I',ROOT/'Common/Inc','-c',HERE/'power_test.c','-o',obj])
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',obj,'--entry','I2c_TestRun','--scatter',sc,'-o',axf])
with axf.open('rb') as f:
 e=ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
# TIMINGR字段计算验证：标准模式最大上升1us、下降0.3us。
# 此项是软件预算，不将计算结果视为板上时序实测。
timing=int(__import__('re').search(r'CAMERA_I2C_TIMING\s+(0x[0-9A-Fa-f]+)',(ROOT/'CM7/BSP/bsp_camera.c').read_bytes().decode('gbk'))[1],16)
from fractions import Fraction
t_presc=Fraction((((timing>>28)&15)+1)*1_000_000_000,120_000_000)
scldel=(((timing>>20)&15)+1)*t_presc;sdadel=((timing>>16)&15)*t_presc
high=(((timing>>8)&255)+1)*t_presc;low=((timing&255)+1)*t_presc
assert scldel>=1000+250 and sdadel>=300-50-Fraction(3_000_000_000,120_000_000)
assert sdadel<=3450-1000-260-Fraction(4_000_000_000,120_000_000)
assert high>=4000 and low>=4700 and high+low>=10000
print('TIMINGR standard-mode budget PASS')
rows=[]
for id in [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,23,25]:
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,b in sections:u.mem_write(a,b)
 u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 u.emu_start(syms['I2c_TestRun']|1,0x01000000,count=1000000)
 row={'id':id,'pass':u.reg_read(UC_ARM_REG_R0)==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000,'failure_line':u.reg_read(UC_ARM_REG_R0)};rows.append(row);print(row)
(OUT/'results.json').write_text(json.dumps(rows,indent=2));raise SystemExit(not all(r['pass'] for r in rows))
