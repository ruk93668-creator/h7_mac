"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1-final/gate';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Tests/UsbLifecycleFinal/reports';REPORT.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin')
target=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(cc/'armcc.exe'),'--cpu','Cortex-M7','--c99','--split_sections','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',str(ROOT/'CM7/BSP/bsp_usb_hal_config.h')]
for inc in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
commands=[]
prior=REPORT/'gate_compile.json'
if prior.exists():(REPORT/('compile-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True);s=(p.stdout+p.stderr).decode('utf-8',errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));print(s)
 (REPORT/'gate_compile.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode or 'warning:' in s.lower():raise RuntimeError(s)
obj=OUT/'hal_out_evidence.o';axf=OUT/'hal_out_evidence.axf'
objects=[]
for name in ['Tests/UsbLifecycleFinal/gate_test.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c']:
 o=OUT/(Path(name).stem+'.o');run(args+['-c',ROOT/name,'-o',o]);objects.append(o)
run([cc/'armlink.exe',*objects,'--entry','Gate_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]

sys.path.insert(0,str(ROOT/'Tests/UsbBulkOutLifecycle'))
from harness import VM,BASE,GINT
names=['EP0 ZLP combined start ordering','Bulk IN ZLP combined start ordering','active EP0 OUT uses generic EPDIS','inactive EP0 OUT no disable write','nonzero IN is not ZLP workaround witness']
rows=[]
for case,name in enumerate(names,1):
 vm=VM(symbols,sections,'Gate_Step',{'g_gate':['result','pointer','outctl','inctl']});checks=[];error=None
 def step(op):return vm.step(op)['g_gate']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  v=step(0);original=v['pointer'];vm.writes=[]
  if case in (1,2,5):
   v=step(1 if case==1 else 2 if case==2 else 4);ep=0 if case==1 else 1
   relevant=[int(x['value'],16) for x in vm.writes if int(x['address'],16)==BASE+0x900+32*ep]
   eq('actual HAL submission reports OK',v['result'],0)
   eq('combined CNAK EPENA witnessed',any(x&(1<<26) and x&(1<<31) for x in relevant),True)
   # Register ordering, not emulated instruction count, is the witness.
   if case!=5:eq('no earlier isolated SNAK phase',any(x&(1<<27) and not x&(1<<26) and not x&(1<<31) for x in relevant),False)
  else:
   if case==3:vm.wr(BASE+0xb00,1<<31);vm.hold_disable.add(0)
   v=step(3);disable=any(int(x['address'],16)==BASE+0xb00 and int(x['value'],16)&(1<<30) for x in vm.writes)
   eq('EPDIS attempt follows actual active state',disable,case==3);eq('Buffer pointer retained',v['pointer'],original)
   eq('actual HAL error vs idle success',v['result'],1 if case==3 else 0)
 except Exception as e:error=str(e)
 ok=error is None and all(x['passed'] for x in checks);rows.append(dict(id=case,name=name,expectation_met=ok,implementation_status='BLOCKED' if case<4 else 'CONTROL_OBSERVATION',error=error,assertions=checks,writes=vm.writes,trace=vm.trace));print(case,'EXPECTED GATE' if ok else 'ERROR',error,flush=True)
report=dict(cases=rows,commands=commands,sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c','Tests/UsbLifecycleFinal/gate_test.c','Tests/UsbLifecycleFinal/run_gate_tests.py']},summary=dict(witness_checks=len(rows),expectations_met=sum(x['expectation_met'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),hardware='NOT_TESTED',meaning='confirmed current register behavior; not a silicon packet failure or verified AHB workaround')
p=REPORT/'gate.json'
if p.exists():(REPORT/('gate-history-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(p.read_bytes())
p.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8');assert all(x['expectation_met'] for x in rows)
