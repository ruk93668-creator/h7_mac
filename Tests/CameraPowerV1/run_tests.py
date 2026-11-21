"""Compile and execute actual Camera BSP with ARMCC5; mock only GPIO/clock hardware."""
from pathlib import Path
import argparse
import subprocess,sys,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
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
run(['D:/32/ARM/ARMCC/Bin/armlink.exe',obj,'--entry','Power_TestRun','--scatter',sc,'-o',axf])
with axf.open('rb') as f:
 e=ELFFile(f);syms={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
rows=[]
for id in range(14):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,b in sections:u.mem_write(a,b)
 u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id)
 u.emu_start(syms['Power_TestRun']|1,0x01000000,count=1000000)
 row={'id':id,'pass':u.reg_read(UC_ARM_REG_R0)==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000,'failure_line':u.reg_read(UC_ARM_REG_R0)};rows.append(row);print(row)
(OUT/'results.json').write_text(json.dumps(rows,indent=2));raise SystemExit(not all(r['pass'] for r in rows))
