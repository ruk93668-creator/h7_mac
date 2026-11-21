"""Run pinned actual TinyUSB USBD/MSC ARM objects, with an explicitly fake DCD."""
from pathlib import Path
import subprocess,json,sys,struct,hashlib,time,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/TinyUSBReplacement'/('r1b-waits-'+str(time.time_ns()));OUT.mkdir(parents=True)
CC=Path('D:/32/ARM/ARMCC/Bin');commands=[]
t=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(CC/'armcc.exe'),'--cpu','Cortex-M7','--c99','--gnu','--split_sections','-O0','-DTINYUSB_MODEL=1','-DSTM32H747xx','-DCORE_CM7']
for inc in ['CM7/TinyUSB','ThirdParty/tinyusb/src']:args+=['-I',str(ROOT/inc)]
for inc in t.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
prior=HERE/'reports/r1b-waits.json'
if prior.exists():(HERE/'reports'/('r1b-waits-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),capture_output=True,cwd=ROOT);s=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));(HERE/'reports/r1b-waits-compile.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode:raise RuntimeError(s)

files=['Tests/TinyUSBReplacement/r2_reset_r1_wait_adapter.c','Tests/TinyUSBReplacement/r1b_wait_test.c','ThirdParty/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c','ThirdParty/tinyusb/src/common/tusb_fifo.c']
objects=[]
for f in files:
 o=OUT/(Path(f).stem+'.o');run(args+['-c',ROOT/f,'-o',o]);objects.append(o)
axf=OUT/'dwc2.axf';run([CC/'armlink.exe',*objects,'--entry','Dwc2_Run','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','--map','--list',OUT/'dwc2.map','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
from unicorn import UC_HOOK_MEM_WRITE,UC_HOOK_MEM_READ
rows=[];BASE=0x40080000
names=['OUT GONAKEFF missing','OUT EPDISD missing after NAK','IN INEPNE missing','IN EPDISD missing after NAK','EP0 OUT never generic disabled','PIO status ZLP combined CNAK EPENA','TX FIFO Flush never clears','Controller DeInit AHB idle absent','RX FIFO Flush never clears']
for case,name in enumerate(names,1):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.mem_map(0x08000000,0x200000);u.mem_map(0x20000000,0x40000);u.mem_map(0x10000000,0x1000);u.mem_map(BASE,0x20000);u.mem_map(0xe0000000,0x100000)
 for a,data in sections:u.mem_write(a,data)
 wr=lambda a,v:u.mem_write(a,struct.pack('<I',v))
 wr(0xe0001000,1);wr(0xe000edfc,1<<24)
 if case in (1,2):wr(BASE+0xb40,1<<31)
 if case==2:wr(BASE+0x14,1<<7)
 if case in (3,4):wr(BASE+0x940,1<<31)
 if case==4:wr(BASE+0x948,1<<6)
 if case==5:wr(BASE+0xb00,1<<31)
 writes=[];reads={}
 def write(cpu,access,a,size,v,ctx):
  if a>=BASE and a<BASE+0x20000:writes.append(dict(address=hex(a),value=hex(v)))
 def read(cpu,access,a,size,v,ctx):
  if a==0xe0001004:wr(a,struct.unpack('<I',cpu.mem_read(a,4))[0]+48000)
  if a>=BASE and a<BASE+0x20000:reads[hex(a)]=reads.get(hex(a),0)+1
 u.hook_add(UC_HOOK_MEM_WRITE,write);u.hook_add(UC_HOOK_MEM_READ,read)
 u.reg_write(UC_ARM_REG_SP,0x2003f000);u.reg_write(UC_ARM_REG_LR,0x10000001);u.reg_write(UC_ARM_REG_R0,case);error=None
 try:u.emu_start(symbols['Dwc2_Run']|1,0x10000000,count=100000)
 except Exception as e:error=str(e)
 vals=struct.unpack('<4I',u.mem_read(symbols['g_dcd_result'],16));returned=u.reg_read(UC_ARM_REG_PC)==0x10000000
 expected=returned and (vals[2]==0 if case in (5,6) else vals[2]==1)
 if case==5:expected=expected and not any(int(v['value'],16)&(1<<30) for v in writes if v['address']==hex(BASE+0xb00))
 if case==6:expected=expected and any(int(v['value'],16)&((1<<26)|(1<<31))==((1<<26)|(1<<31)) for v in writes if v['address']==hex(BASE+0x900))
 row=dict(case=case,name=name,expectation_met=bool(expected and not error),returned=returned,raw=list(vals),reads=reads,writes=writes,error=error,pc=hex(u.reg_read(UC_ARM_REG_PC)));rows.append(row);print(row,flush=True)
report=dict(kind='bounded failure regression of original six hangs',cases=rows,commands=commands,artifact=str(axf),hardware='NOT_TESTED',meaning='Each call must return; actual firmware DWT deadline/clock validity decides failure. Emulator instruction budget remains only a test harness watchdog.',sources={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files+['ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c']})
(HERE/'reports/r1b-waits.json').write_text(json.dumps(report,indent=2),encoding='utf-8');raise SystemExit(not all(r['expectation_met'] for r in rows))
