from pathlib import Path
import sys,subprocess,json
R=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent;sys.path.insert(0,str(R/'Tests/BleB1/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
objects=[]
for p in [H/'h5_contract.c',R/'CM7/Platform/bt_hci_h5.c']:
 o=H/'build'/(p.stem+'.o');subprocess.run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O2','-I',str(R/'Common/Inc'),'-I',str(R/'CM7/Platform'),'-c',str(p),'-o',str(o)],check=True);objects.append(str(o))
a=H/'build/h5.axf';subprocess.run(['D:/32/ARM/ARMCC/Bin/armlink.exe',*objects,'--entry','H5_Test','--scatter',str(R/'Tests/UsbDeviceStage1/test.sct'),'-o',str(a)],check=True)
u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
for addr,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000)]:u.mem_map(addr,size)
with a.open('rb') as f:
 e=ELFFile(f);entry=e.header['e_entry']
 for s in e.iter_sections():
  if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']:u.mem_write(s['sh_addr'],s.data())
u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.emu_start(entry|1,0x01000000,count=5000000)
line=u.reg_read(UC_ARM_REG_R0);ok=line==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
(H/'h5_results.json').write_bytes((json.dumps({'pass':ok,'failure_line':line,'hardware_verified':False,'coverage':['link_timeout','config_crc','reliable_delivery','duplicate','bad_crc','partial_write','valid_ack','retry_exhaustion']},indent=2)+'\n').encode());print('H5',ok,line);raise SystemExit(not ok)
