"""Run pinned actual TinyUSB USBD/MSC ARM objects, with an explicitly fake DCD."""
from pathlib import Path
import subprocess,json,sys,struct,hashlib,time,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/TinyUSBReplacement'/('stack-'+str(time.time_ns()));OUT.mkdir(parents=True)
CC=Path('D:/32/ARM/ARMCC/Bin');commands=[]
t=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(CC/'armcc.exe'),'--cpu','Cortex-M7','--c99','--gnu','--split_sections','-O0','-DTINYUSB_MODEL=1','-DSTM32H747xx','-DCORE_CM7']
for inc in ['CM7/TinyUSB','ThirdParty/tinyusb/src']:args+=['-I',str(ROOT/inc)]
for inc in t.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
prior=HERE/'reports/stack.json'
if prior.exists():(HERE/'reports'/('stack-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),capture_output=True,cwd=ROOT);s=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));(HERE/'reports/stack-compile.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode:raise RuntimeError(s)
files=['ThirdParty/tinyusb/src/tusb.c','ThirdParty/tinyusb/src/common/tusb_fifo.c','ThirdParty/tinyusb/src/device/usbd.c','ThirdParty/tinyusb/src/class/msc/msc_device.c','CM7/TinyUSB/tinyusb_ram.c','CM7/TinyUSB/usb_descriptors.c','CM7/Service/usb_msc_ram.c','Tests/TinyUSBReplacement/stack_test.c']
objects=[]
for f in files:
 o=OUT/(Path(f).stem+'.o');run(args+['-c',ROOT/f,'-o',o]);objects.append(o)
axf=OUT/'stack.axf';run([CC/'armlink.exe',*objects,'--entry','Test_Run','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','--map','--list',OUT/'stack.map','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
rows=[]
names=['enumeration descriptors SET_ADDRESS GET_MAX_LUN','INQUIRY CBW/Data/CSW','multi-chunk READ10 3 sectors','RAM WRITE10 and READ10 roundtrip','12-byte REQUEST SENSE','BOT Reset holds pending_io buffer','Bus Reset reconfigure holds pending_io','repeated BOT Reset Clear HALT late IO','drained software Stop/Restart fake DCD','old completion after Bus Reset','EP0 SETUP preemption software queue','active Data IN BOT Reset actual stall-clear fallback']
for case,name in enumerate(names,1):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);u.mem_map(0x08000000,0x200000);u.mem_map(0x20000000,0x40000);u.mem_map(0x10000000,0x1000)
 for a,data in sections:u.mem_write(a,data)
 u.reg_write(UC_ARM_REG_SP,0x2003f000);u.reg_write(UC_ARM_REG_LR,0x10000001);u.reg_write(UC_ARM_REG_R0,case);error=None
 try:u.emu_start(symbols['Test_Run']|1,0x10000000,count=3000000)
 except Exception as e:error=str(e)
 values=struct.unpack('<8I',u.mem_read(symbols['g_result'],32));returned=u.reg_read(UC_ARM_REG_PC)==0x10000000;passed=returned and not error and values[0]==0
 row=dict(case=case,name=name,passed=passed,returned=returned,failed_line=values[0],checks=values[1],raw=list(values),error=error);rows.append(row);print(row,flush=True)
report=dict(cases=rows,summary=dict(cases=len(rows),passed=sum(r['passed'] for r in rows),assertions=sum(r['checks'] for r in rows)),sources={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files},commands=commands,artifact=str(axf),hardware='NOT_TESTED',boundary='Real TinyUSB stack and RAM backend; fake synchronous DCD, OS_NONE deterministic test only, not a DWC2/H7/FreeRTOS or Windows acceptance')
prior.write_text(json.dumps(report,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in rows))
