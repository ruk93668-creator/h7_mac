"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1-final/worker';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Tests/UsbLifecycleFinal/reports';REPORT.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin')
target=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(cc/'armcc.exe'),'--cpu','Cortex-M7','--c99','--split_sections','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',str(ROOT/'CM7/BSP/bsp_usb_hal_config.h')]
for inc in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
commands=[]
prior=REPORT/'worker_compile.json'
if prior.exists():(REPORT/('compile-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True);s=(p.stdout+p.stderr).decode('utf-8',errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));print(s)
 (REPORT/'worker_compile.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode or 'warning:' in s.lower():raise RuntimeError(s)
obj=OUT/'hal_out_evidence.o';axf=OUT/'hal_out_evidence.axf'
objects=[]
for name in ['Tests/UsbLifecycleFinal/worker_guard_test.c']:
 o=OUT/(Path(name).stem+'.o');run(args+['-c',ROOT/name,'-o',o]);objects.append(o)
run([cc/'armlink.exe',*objects,'--entry','Worker_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]

sys.path.insert(0,str(ROOT/'Tests/UsbBulkOutLifecycle'))
from harness import VM
rows=[]
for case in range(1,4):
 vm=VM(symbols,sections,'Worker_Step',{'g_worker_guard':['result','mail','drained','closes','epoch','in_open','out_open','mask','unexpected']});checks=[]
 def step(op):return vm.step(op)['g_worker_guard']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 v=step(0);eq('real mailbox submit',v['result'],0)
 if case in (1,2):
  v=step(1);eq('real mailbox DRAIN while device not quiescent',v['mail'],3);eq('not drained',v['drained'],0)
  v=step(2 if case==1 else 4);eq('DeInit BUSY keeps worker',v['result'],1);eq('no endpoint Close before consumer proof',v['closes'],0);eq('old request identity kept',v['epoch'],1)
  v=step(3);eq('only device quiescence allows mailbox retirement',v['drained'],1)
  v=step(2);eq('both independent endpoints now attempted',v['closes'],2);eq('USB refusal not reported success',v['result'],3)
 else:
  v=step(2);eq('unstarted queue cancel has no DMA reference',v['drained'],1);eq('USB still refuses closure',v['result'],3)
 eq('IN resource retained',v['in_open'],1);eq('OUT resource retained',v['out_open'],1);eq('critical mask restored',v['mask'],0);eq('no unexpected protocol or USB proof stub',v['unexpected'],0)
 rows.append(dict(id=case,passed=all(x['passed'] for x in checks),assertions=checks,trace=vm.trace));print(case,rows[-1]['passed'],[x for x in checks if not x['passed']])
report=dict(cases=rows,commands=commands,summary=dict(cases=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),scope='consumer guard only; USB Close refuses, no USB/SD silicon proof fabricated',hardware='NOT_TESTED',sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['CM7/Service/usb_msc_class.c','CM7/Service/usb_msc_bot.c','CM7/Service/usb_msc_bot.h','Tests/UsbLifecycleFinal/worker_guard_test.c','Tests/UsbLifecycleFinal/run_worker_tests.py']})
prior=REPORT/'worker.json'
if prior.exists():(REPORT/('worker-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
(REPORT/'worker.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert all(x['passed'] for x in rows)
