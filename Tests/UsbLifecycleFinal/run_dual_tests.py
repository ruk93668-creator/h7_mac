"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1-final/dual';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Tests/UsbLifecycleFinal/reports';REPORT.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin')
target=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(cc/'armcc.exe'),'--cpu','Cortex-M7','--c99','--split_sections','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',str(ROOT/'CM7/BSP/bsp_usb_hal_config.h')]
for inc in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
commands=[]
prior=REPORT/'compile.json'
if prior.exists():(REPORT/('compile-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True);s=(p.stdout+p.stderr).decode('utf-8',errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));print(s)
 (REPORT/'compile.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode or 'warning:' in s.lower():raise RuntimeError(s)
obj=OUT/'hal_out_evidence.o';axf=OUT/'hal_out_evidence.axf'
objects=[]
for name in ['Tests/UsbLifecycleFinal/dual_stop_test.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c','Tests/UsbBulkOutLifecycle/boundary_stubs.c']:
 o=OUT/(Path(name).stem+'.o');run(args+['-c',ROOT/name,'-o',o]);objects.append(o)
run([cc/'armlink.exe',*objects,'--entry','Final_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]



sys.path.insert(0,str(ROOT/'Tests/UsbBulkOutLifecycle'))
from harness import VM,BASE,GINT,MASK,DCTL
from unicorn.arm_const import UC_ARM_REG_PRIMASK
fields='result intent state endpoints pending registered proven blocked first_error out_owned in_owned class_memory started irq out_attempt in_attempt forbidden_release_calls out1_owned'.split()
class DualVM(VM):
 def read(self,cpu,access,a,size,value,ctx):
  # Explicit MMIO evidence; not software proof writes.
  if a==BASE+0x920:
   v=self.rd(a)
   if v&(1<<27):self.wr(a,v|(1<<17));self.event_in(1,1<<6)
   if v&(1<<30) and not getattr(self,'hold_in',False):self.wr(a,self.rd(a)&~((1<<31)|(1<<30)));self.event_in(1,2)
  super().read(cpu,access,a,size,value,ctx)
 def event_in(self,ep,bits):self.wr(BASE+0x908+32*ep,self.rd(BASE+0x908+32*ep)|bits)
rows=[]
names=['both directions Pending same Controller identity','completion and Commit without unsafe Controller handoff','OUT timeout retains buffer while IN drains','IN disable timeout retains source while OUT drains','Reset keeps old resources','Disconnect keeps old resources','submission fence blocks both directions','ordinary reentrant Busy is not Pending','Controller identity exhaustion','foreign endpoint Attempt is not silently adopted','repeat blocked Stop preserves identity','Core DeInit cannot bypass Controller fence','Reset after endpoint proof keeps Class','Disconnect after proof keeps Class','legacy release APIs cannot bypass consumer gate','never armed IN still has real stop proof','three active endpoint transfers share one intent']
for case,name in enumerate(names,1):
 vm=DualVM(symbols,sections,'Final_Step',{'g_final':fields});checks=[];error=None
 def step(op,arg=0):return vm.step(op,arg)['g_final']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  v=step(0);eq('fixture and actual Enable/Start/Open',v['result'],0);vm.wr(BASE+0x40,0x4f54310a)
  if case==8:
   v=step(8);eq('ordinary Busy rejected',v['result'],0xfffffffb);eq('no registered cancellation',v['registered'],0);eq('no Controller identity',v['intent'],0)
  elif case==9:
   step(9);v=step(3);eq('exhaustion rejected',v['result'],0xfffffffa);eq('no identity reuse',v['intent'],0)
  else:
   eq('OUT admitted',step(1)['result'],0)
   if case!=16:eq('IN admitted',step(2)['result'],0)
   if case==17:eq('second OUT active',step(5)['result'],0)
   if case==10:step(10)
   if case==3:vm.auto_nak=False
   if case==4:vm.hold_in=True
   v=step(3);identity=v['intent'];eq('Controller identity exists',identity!=0,True)
   if case==10:
    eq('foreign Attempt is not own Pending',v['registered'],0);eq('foreign source retained',v['out_owned'],1)
   else:
    eq('IN and OUT both pending',v['pending']>=2,True);eq('not a fake ordinary Busy',v['registered'],1)
    if case==7:eq('new OUT fenced',step(5)['result']!=0,True);eq('new IN fenced',step(6)['result']!=0,True)
    if case==5:vm.wr(GINT,vm.rd(GINT)|(1<<12))
    if case==6:vm.wr(BASE+4,4);vm.wr(GINT,vm.rd(GINT)|(1<<2))
    for i in range(45):
     vm.levels()
     if vm.rd(GINT)&vm.rd(MASK):step(4)
     v=step(3);eq('same intent across rounds',v['intent'],identity)
     if v['blocked']:break
    if case in (3,4):step(7,1001);v=step(3)
    if case in (1,2,7,11,12,13,14,15,16,17):
     eq('endpoint proofs collected',v['proven'],1);eq('EP0/consumer gate enforced',v['blocked'],1)
     eq('no false Controller success',v['result'],0xfffffff9);eq('OUT owner retired by real Commit',v['out_owned'],0);eq('IN owner retired by real Commit',v['in_owned'],0)
     if case==11:v=step(3);eq('repeat keeps intent',v['intent'],identity)
     if case==12:v=step(11);eq('synchronous Core DeInit vetoed',v['result'],1)
     if case in (13,14):v=step(12 if case==13 else 13)
     if case==15:v=step(14);eq('all legacy resource APIs refuse',v['result'],0)
     eq('no forbidden Core or Class release callback',v['forbidden_release_calls'],0)
     if case==16:eq('unarmed IN used real attempt',v['in_attempt']!=0,True)
     if case==17:eq('second OUT retired only by proof',v['out1_owned'],0)
    elif case==3:eq('OUT timeout retains old buffer',v['out_owned'],1);eq('independent IN drained',v['in_owned'],0)
    elif case==4:eq('IN timeout retains source',v['in_owned'],1);eq('independent OUT drained',v['out_owned'],0)
    else:eq('lifecycle boundary retains OUT',v['out_owned'],1)
   eq('Class memory not released without consumer proof',v['class_memory'],1);eq('controller not stopped past gate',v['started'],1)
  eq('USB IRQ remains enabled',step(99)['irq'],1);eq('short critical section restored',vm.trace[-1]['primask'],0);eq('no shared RX flush',vm.rx_flushes,0)
 except Exception as e:error=str(e)
 passed=error is None and all(c['passed'] for c in checks);rows.append(dict(id=case,name=name,passed=passed,error=error,assertions=checks,trace=vm.trace,writes=vm.writes,reads=vm.reads));print(case,'PASS' if passed else 'FAIL',error,[x for x in checks if not x['passed']],flush=True)
report=dict(sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['CM7/Platform/usb_device.c','CM7/Platform/usb_device.h','Tests/UsbLifecycleFinal/dual_stop_test.c','Tests/UsbLifecycleFinal/run_dual_tests.py']},commands=commands,baseline='9d51d0c5d5b62d04af3be968ec452dae8835c0c1',cases=rows,summary=dict(cases=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),hardware='NOT_TESTED',scope='bounded opt-in endpoint proof aggregation; no EP0 or consumer ticket fabricated')
p=REPORT/'dual.json'
if p.exists():(REPORT/('dual-history-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(p.read_bytes())
p.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert all(x['passed'] for x in rows)
