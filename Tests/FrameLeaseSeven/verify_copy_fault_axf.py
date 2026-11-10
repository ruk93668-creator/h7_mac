"""Execute the actual formal AXF error path, including final linker resolution."""
from pathlib import Path
import sys,json,struct,hashlib
R=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent
sys.path.insert(0,str(R/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
p=R/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf'
with p.open('rb') as f:
 e=ELFFile(f);sy={s.name:s for s in e.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
 assert sy['BSP_SDMMC_CaptureCopyFault']['st_info']['bind']=='STB_GLOBAL'
 symbols={n:s['st_value'] for n,s in sy.items()}
rows=[]
for case in range(4):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x200000),(0x20000000,0x20000),(0x24000000,0x80000),(0x30000000,0x48000),(0x38000000,0x1000),(0x40020000,0x1000),(0x48020000,0x1000),(0x52007000,0x1000),(0xE000E000,0x2000),(0x01000000,0x1000)]:
  u.mem_map(a,n)
 for a,data in sections:u.mem_write(a,data)
 def put(a,v):u.mem_write(a,struct.pack('<I',v&0xffffffff))
 root=symbols['g_sd_copy_debug'];live=[3 if case==2 else 2,0 if case==1 else 1,0xD0480000,0x24000000,1024,0x30]
 u.mem_write(root,struct.pack('<6I',*live));put(symbols['s_event_dma_error'],2)
 put(0x40020000,0x31);put(0x40020010,0x00865517);put(0x40020014,0x77c);put(0x40020024,0xa7)
 put(0x48020008,0x19);put(0x52007050,1);put(0x52007058,0x24000000)
 def call(result):
  u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,result&0xffffffff)
  u.emu_start(symbols['Dcmi_RecordError']|1,0x01000000,count=100000)
  assert u.reg_read(UC_ARM_REG_PC)==0x01000000
 call(-9)
 fault=bytes(u.mem_read(root+24,68));words=struct.unpack('<17I',fault)
 assert words[0]==2 and words[1]==(0 if case==2 else 1) and words[2]==live[1]
 assert words[4:9]==(0xD0480000,0x24000000,1024,0x30,0x31)
 assert words[9]==2 and words[14]==1 and words[15]==0x24000000 and words[16]==0xfffffff7
 if case==3:
  put(root+8,0x20000000);call(-8);assert bytes(u.mem_read(root+24,68))==fault
 rows.append(dict(case=case,passed=True,fault_words=list(words)))
report=dict(axf=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),capture_address=hex(symbols['BSP_SDMMC_CaptureCopyFault']),total=4,passed=4,failed=0,cases=rows,hardware_verified=False)
(H/'copy_fault_axf_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Formal AXF: strong SD implementation retained; Dcmi_RecordError captures 4/4 PASS')
