"""Check memory-access width of the final formal AXF, not just source spelling."""
from pathlib import Path
import sys,json,struct,hashlib
R=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_PC
p=R/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf'
with p.open('rb') as f:
 e=ELFFile(f);sy={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
rows=[]
for length in [1,17,8192]:
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x200000),(0x20000000,0x20000),(0x24000000,0x80000),(0xD0000000,0x800000),(0x30000000,0x48000),(0x38000000,0x1000),(0x40020000,0x1000),(0x01000000,0x1000)]:
  u.mem_map(a,n)
 for a,data in sections:u.mem_write(a,data)
 src=0xD04B2B40;dst=0x24000000;data=bytes((i^0x5a)&255 for i in range(length))
 u.mem_write(src,data);u.mem_write(dst,bytes([0xc7])*(length+4))
 reads=[];writes=[];calls=[]
 def read(uc,kind,a,n,v,d):reads.append((a,n))
 def write(uc,kind,a,n,v,d):writes.append((a,n))
 u.hook_add(UC_HOOK_MEM_READ,read,begin=src,end=src+length-1)
 u.hook_add(UC_HOOK_MEM_WRITE,write,begin=dst,end=dst+length-1)
 def library(uc,a,n,d):calls.append(a)
 for address in set(v&~1 for n,v in sy.items() if 'memcpy' in n.lower()):
  u.hook_add(UC_HOOK_CODE,library,begin=address,end=address)
 u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001)
 u.reg_write(UC_ARM_REG_R0,dst);u.reg_write(UC_ARM_REG_R1,src);u.reg_write(UC_ARM_REG_R2,length)
 u.emu_start(sy['Sdmmc_CopyMarked']|1,0x01000000,count=1000000)
 assert u.reg_read(UC_ARM_REG_PC)==0x01000000
 assert reads==[(src+i,1) for i in range(length)] and writes==[(dst+i,1) for i in range(length)]
 assert bytes(u.mem_read(dst,length))==data and bytes(u.mem_read(dst+length,4))==bytes([0xc7])*4
 assert not calls
 rows.append(dict(length=length,read_count=len(reads),write_count=len(writes),access_bytes=1,memcpy_calls=len(calls),passed=True))
(H/'sd_byte_copy_axf_results.json').write_text(json.dumps(dict(axf=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),total=3,passed=3,failed=0,cases=rows,hardware_verified=False),indent=2),encoding='utf-8')
print('Formal AXF byte-copy: 3/3 PASS; 8192 single-byte reads/writes; no memcpy call')
