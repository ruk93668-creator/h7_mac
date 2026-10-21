import argparse
from pathlib import Path
import sys

parser = argparse.ArgumentParser()
parser.add_argument('image', type=Path)
parser.add_argument('--core', choices=['CM7','CM4'], required=True)
parser.add_argument('--deps', type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.deps))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile

with args.image.open('rb') as f:
    elf=ELFFile(f)
    entry=elf.header['e_entry']
    segments=[(s['p_vaddr'],s.data()) for s in elf.iter_segments() if s['p_type']=='PT_LOAD' and s['p_filesz']]
names=['Initcall success ordering','Initcall first failure preserves -9 and skips remaining calls/levels',
       'Return constants, IPC fault OR, shared layout, IPC service', 'Event return semantics',
       'Timer parameter, stale handle and capacity return semantics','Unimplemented Device returns']
failed=False
for case in range(5 if args.core=='CM4' else 6):
    uc=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    uc.mem_map(0x08000000,0x100000)
    uc.mem_map(0x20000000,0x40000)
    uc.mem_map(0x01000000,0x1000)
    for address,data in segments: uc.mem_write(address,data)
    uc.reg_write(UC_ARM_REG_SP,0x2003FFF0)
    uc.reg_write(UC_ARM_REG_LR,0x01000001)
    uc.reg_write(UC_ARM_REG_R0,case)
    uc.emu_start(entry|1,0x01000000,timeout=10000000,count=10000000)
    result=uc.reg_read(UC_ARM_REG_R0)
    passed=result==0 and uc.reg_read(UC_ARM_REG_PC)==0x01000000
    failed|=not passed
    print(f'{args.core} {"PASS" if passed else "FAIL"}: {names[case]}; assertion line={result}')
raise SystemExit(1 if failed else 0)
