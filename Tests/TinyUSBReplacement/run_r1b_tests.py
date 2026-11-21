"""Run actual TinyUSB DWC2/USBD/MSC ARM objects against explicit hardware MMIO model."""
from pathlib import Path
import subprocess,json,sys,struct,hashlib,time,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/TinyUSBReplacement'/('r1b-'+str(time.time_ns()));OUT.mkdir(parents=True)
CC=Path('D:/32/ARM/ARMCC/Bin');commands=[]
t=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(CC/'armcc.exe'),'--cpu','Cortex-M7','--c99','--gnu','--split_sections','-O0','-DTINYUSB_MODEL=1','-DSTM32H747xx','-DCORE_CM7']
for inc in ['CM7/TinyUSB','ThirdParty/tinyusb/src']:args+=['-I',str(ROOT/inc)]
for inc in t.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
prior=HERE/'reports/r1b.json'
if prior.exists():(HERE/'reports'/('r1b-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),capture_output=True,cwd=ROOT);s=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));(HERE/'reports/r1b-compile.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode:raise RuntimeError(s)
files=['Tests/TinyUSBReplacement/r1b_core.c','Tests/TinyUSBReplacement/r1b_dcd.c','ThirdParty/tinyusb/src/portable/synopsys/dwc2/dwc2_common.c','ThirdParty/tinyusb/src/tusb.c','ThirdParty/tinyusb/src/common/tusb_fifo.c','ThirdParty/tinyusb/src/class/msc/msc_device.c','CM7/TinyUSB/tinyusb_ram.c','CM7/TinyUSB/usb_descriptors.c','CM7/Service/usb_msc_ram.c']
objects=[]
for f in files:
 o=OUT/(Path(f).stem+'.o');run(args+['-c',ROOT/f,'-o',o]);objects.append(o)
axf=OUT/'stack.axf';run([CC/'armlink.exe',*objects,'--entry','R1B_Step','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','--map','--list',OUT/'stack.map','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]

# VM and test scenarios appended below.

sys.path.insert(0,str(ROOT/'Tests/UsbBulkOutLifecycle'))
from harness import VM,BASE,GINT,MASK,DCTL,IMASK
from unicorn import UC_HOOK_MEM_READ
from unicorn.arm_const import UC_ARM_REG_IPSR
class Model(VM):
 def __init__(self):
  super().__init__(symbols,sections,'R1B_Step',{'g_observation':'result busy phase held generation fault owner old_byte new_byte device_fault core_fault tick status_arms out_busy'.split()})
  self.cpu.mem_map(0x58000000,0x100000) # H747 RCC/PWR register aperture
  self.hold_in=False;self.hold_nak_in=False;self.hold_flush=False;self.hold_reset=False;self.stall_cycles=False
  self.cpu.hook_add(UC_HOOK_MEM_READ,self.cycle_read,begin=0xe0001004,end=0xe0001007)
  self.wr(0xe0001000,1);self.wr(0xe000edfc,1<<24)
  self.wr(BASE+0x48,8<<10);self.wr(BASE+0x40,0x4f54310a)
 def cycle_read(self,cpu,access,a,size,value,ctx):
  if not self.stall_cycles:self.wr(a,self.rd(a)+48000)
 def step(self,op,arg=0):
  if op==4:self.wr(0xe0001004,(self.rd(0xe0001004)+arg*480000)&0xffffffff)
  self.cpu.reg_write(UC_ARM_REG_IPSR,16 if op==3 else 0)
  try:return super().step(op,arg)
  finally:self.cpu.reg_write(UC_ARM_REG_IPSR,0)
 def read(self,cpu,access,a,size,value,ctx):
  if a==BASE+0x20 and self.q:
   status=self.q[0][0]; ep=status&15; kind=(status>>17)&15; n=(status>>4)&0x7ff
   if kind==2:
    address=BASE+0xb10+32*ep; size=self.rd(address)
    self.wr(address,(size&~0x7ffff)|max(0,(size&0x7ffff)-n))
  if a==BASE+0x10:
   if self.hold_flush:return
   if not self.hold_reset:self.wr(a,self.rd(a)&~1) # CSRST hardware self-clear
  if a==BASE+0x10 and self.hold_flush:return
  if a==BASE+0x940:
   value=self.rd(a)
   if value&(1<<27) and not self.hold_nak_in:self.wr(BASE+0x948,self.rd(BASE+0x948)|(1<<6))
   if value&(1<<30) and not self.hold_in:
    self.wr(a,value&~((1<<30)|(1<<31)));self.wr(BASE+0x948,self.rd(BASE+0x948)|2)
  super().read(cpu,access,a,size,value,ctx)
rows=[]
names=['OUT SUCCESS','IN SUCCESS','OUT missing NAK bounded FAILED','OUT missing Disable bounded FAILED','IN missing NAK bounded FAILED','IN missing Disable bounded FAILED','IN FIFO flush bounded FAILED','OUT queued old RX drained','old queued XFRC cannot retire new transfer','duplicate cancel remains PENDING','other OUT can arm after NAK release','USB Reset during cancel retains old buffer','EP0 SETUP during OUT cancel','late raw XFRC before rearm refuses submit']
for case,name in enumerate(names,1):
 v=Model();checks=[];error=None
 def step(op,arg=0):return v.step(op,arg)['g_observation']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def irq():
  v.levels()
  if v.rd(GINT)&v.rd(MASK):step(3)
 def drive(limit=24):
  for i in range(limit):
   irq();x=step(2)
   if x['phase']==5 or x['device_fault']:return x
  raise AssertionError('no completion within model progress steps')
 try:
  is_in=case in (2,5,6,7,9,14)
  ep=0x82 if is_in else 2
  step(0,ep);eq('real submit',step(1)['result'],1)
  if case==3:v.auto_nak=False
  if case==4:v.hold_disable.add(2)
  if case==5:v.hold_nak_in=True
  if case==6:v.hold_in=True
  if case==7:v.hold_flush=True
  if case==8:v.queue(2,b'X'*31)
  x=step(2);eq('PENDING no success',x['result'],0);eq('busy retained',x['busy'],1);eq('buffer retained',x['held'],1);eq('no Status ZLP',x['status_arms'],0)
  if case==10:
   eq('repeat pending',step(2)['result'],0);eq('pending DeInit refused before disconnect',step(11)['result'],0);eq('pullup remains connected',v.rd(DCTL)&2,0);eq('Clear HALT keeps pending busy',step(14)['busy'],1);eq('release refused while pending',step(15)['result'],0);eq('ISR old event cannot release',step(16)['busy'],1)
  if case==12:
   v.wr(GINT,v.rd(GINT)|(1<<12));step(3);x=step(2)
   eq('Reset fails safely',x['device_fault'],1);eq('old source retained',x['held'],1)
  elif case in (3,4,5,6,7):
   for i in range(10):irq();x=step(2)
   eq('not falsely completed',x['held'],1)
   step(4,51);x=step(2);eq('deadline failure',x['device_fault'],1);eq('core fault',x['core_fault'],1);eq('source retained',x['held'],1);eq('new submission refused',step(5)['result'],0)
  else:
   if case==13:
    v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=4)
   x=drive();eq('SUCCESS',x['result'],1);eq('retired phase',x['phase'],5);eq('source returned',x['held'],0);eq('busy cleared',x['busy'],0);eq('not fault',x['device_fault'],0)
   if not is_in:eq('NAK owner released',x['owner'],0xffffffff);eq('global NAK clear',v.rd(DCTL)&8,0)
   if case==8:eq('cancelled payload never writes old buffer',x['old_byte'],0xa5)
   if case==9:
    eq('next transfer armed',step(5)['result'],1);x=step(6);eq('old generation event ignored',x['busy'],1)
   if case==11:
    eq('other OUT admission',step(7)['result'],1);v.queue(1,b'Q'*64);v.queue(1,kind=3);v.complete(1);irq();eq('other OUT PIO data received',step(4,0)['new_byte'],ord('Q'))
   if case==14:
    v.wr(BASE+0x948,1);eq('stale raw terminal blocks rearm',step(5)['result'],0)
  eq('no shared RX FIFO flush',v.rx_flushes,0)
 except Exception as e:error=str(e)
 row=dict(case=case,name=name,passed=not error and all(x['passed'] for x in checks),checks=checks,error=error,trace=v.trace,register_writes=v.writes,rx_reads=v.reads)
 rows.append(row);print(case,name,row['passed'],error,[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],flush=True)

for is_write in (False,True):
 v=Model();checks=[];error=None
 def step(op,arg=0):return v.step(op,arg)['g_observation']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def irq():
  v.levels()
  if v.rd(GINT)&v.rd(MASK):step(3)
 try:
  step(0,2 if is_write else 0x82)
  eq('real class open',step(9)['result'],1)
  cdb=bytes([0x2a if is_write else 0x28,0,0,0,0,0,0,0,1,0])+b'\0'*6
  cbw=struct.pack('<IIIBBB16s',0x43425355,77,512,0 if is_write else 0x80,0,10,cdb)
  v.queue(2,cbw);v.queue(2,kind=3);v.complete(2);irq();step(8)
  if not is_write:step(10);step(8)
  # Submit a real SETUP through the one RXFLVL pop/copy path, not a fake class reset.
  v.queue(0,bytes([0x21,0xff,0,0,0,0,0,0]),kind=6);v.queue(0,kind=4);irq();x=step(8)
  eq('reset pending without status',x['status_arms'],0);eq('old transfer held while pending',x['held'],1)
  for i in range(24):
   irq();x=step(8)
   if x['status_arms']:break
   eq('pending has no status',x['status_arms'],0)
  eq('status only after stop',x['status_arms'],1);eq('next CBW armed',x['out_busy'],1)
  eq('no device fault',x['device_fault'],0)
  if is_write:eq('Global NAK returned',v.rd(DCTL)&8,0)
 except Exception as e:error=str(e)
 rows.append(dict(case=len(rows)+1,name='real MSC '+('OUT' if is_write else 'IN')+' BOT Reset deferred status',passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,trace=v.trace,register_writes=v.writes,rx_reads=v.reads))
 print(rows[-1]['name'],rows[-1]['passed'],error,[(c['name'],c['actual'],c['expected']) for c in checks if not c['passed']],flush=True)

for scenario in ['ten controller Stop Start cycles','controller reset stuck retains state','EP0 active IN setup ISR refuses wait','superseded BOT Reset never sends stale status']:
 v=Model();checks=[];error=None
 def step(op,arg=0):return v.step(op,arg)['g_observation']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def irq():
  v.levels()
  if v.rd(GINT)&v.rd(MASK):step(3)
 try:
  if scenario.startswith('ten'):
   previous=0
   for cycle in range(10):
    step(0,2);eq('submit',step(1)['result'],1);step(2)
    for i in range(24):
     irq();x=step(2)
     if x['phase']==5:break
    eq('cancel SUCCESS',x['result'],1);eq('monotonic transfer generation',x['generation']>previous,True);previous=x['generation']
    eq('real tud_deinit',step(11)['result'],1);eq('real tud_rhport_init',step(12)['result'],1)
  elif scenario.startswith('controller'):
   step(0,2);step(1);step(2)
   for i in range(24):
    irq();x=step(2)
    if x['phase']==5:break
   v.hold_reset=True;eq('deinit rejects stuck CSRST',step(11)['result'],0);eq('fault recorded',step(11)['device_fault'],1);eq('new session refused',step(12)['result'],0)
  elif scenario.startswith('EP0'):
   step(0,2);eq('EP0 IN real submission',step(13)['result'],1)
   v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=4);irq();x=step(8)
   eq('unproven EP0 disable faults',x['device_fault'],1);eq('no next transfer',step(1)['result'],0)
  else:
   step(0,2);eq('class open',step(9)['result'],1)
   cmd=bytes([0x2a,0,0,0,0,0,0,0,1,0])+b'\0'*6
   v.queue(2,struct.pack('<IIIBBB16s',0x43425355,91,512,0,0,10,cmd));v.queue(2,kind=3);v.complete(2);irq();step(8)
   v.queue(0,bytes([0x21,0xff,0,0,0,0,0,0]),kind=6);v.queue(0,kind=4);irq();eq('no reset status while pending',step(8)['status_arms'],0)
   v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=4);irq();x=step(8);eq('new descriptor IN',x['status_arms'],1)
   for i in range(24):irq();x=step(8)
   eq('old reset cannot add status',x['status_arms'],1);eq('descriptor remains 18 bytes',v.rd(BASE+0x910)&0x7ffff,18)
 except Exception as e:error=str(e)
 row=dict(case=len(rows)+1,name=scenario,passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,trace=v.trace,register_writes=v.writes,rx_reads=v.reads);rows.append(row)
 print(scenario,row['passed'],error,[(c['name'],c['actual'],c['expected']) for c in checks if not c['passed']],flush=True)

for scenario in ['foreign Global NAK owner','release completion missing','normal IN XFRC queued before cancellation','old queued XFRC during pending','DWT clock unavailable','DWT clock stalls','Disconnect during pending','Reset latched before cancellation poll']:
 v=Model();checks=[];error=None
 def step(op,arg=0):return v.step(op,arg)['g_observation']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def irq():
  v.levels()
  if v.rd(GINT)&v.rd(MASK):step(3)
 try:
  is_in=scenario.startswith('normal') or scenario.startswith('old')
  step(0,0x82 if is_in else 2);eq('submit',step(1)['result'],1)
  if scenario.startswith('normal'):
   v.hold_nak_in=True
   v.wr(BASE+0x940,v.rd(BASE+0x940)&~(1<<31));v.wr(BASE+0x948,1);irq()
  if scenario.startswith('foreign'):v.wr(GINT,v.rd(GINT)|(1<<7));v.wr(DCTL,v.rd(DCTL)|8)
  if scenario.startswith('release'):v.hold_release=True
  if scenario=='DWT clock unavailable':v.wr(0xe0001000,0)
  step(2)
  if scenario.startswith('old'):
   eq('queued old event while pending keeps busy',step(6)['busy'],1)
  if scenario.startswith('Reset latched'):v.wr(GINT,v.rd(GINT)|(1<<12))
  if scenario.startswith('Disconnect'):
   v.wr(BASE+4,4);v.wr(MASK,v.rd(MASK)|(1<<2));v.wr(GINT,v.rd(GINT)|(1<<2));irq()
  if scenario=='DWT clock stalls':v.stall_cycles=True
  for i in range(20):
   if not scenario.startswith('Reset latched'):irq()
   x=step(2)
  if scenario.startswith('normal') or scenario.startswith('old'):
   eq('success after real completion/disable evidence',x['result'],1);eq('source retired',x['held'],0)
  else:
   step(4,51);x=step(2);eq('observable FAILED',x['device_fault'],1);eq('old buffer retained',x['held'],1);eq('new transfer refused',step(5)['result'],0)
   if scenario.startswith('foreign'):eq('foreign NAK not cleared',v.rd(DCTL)&8,8)
   if scenario.startswith('release'):eq('unreleased owner retained',x['owner'],2)
 except Exception as e:error=str(e)
 row=dict(case=len(rows)+1,name=scenario,passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,trace=v.trace,register_writes=v.writes,rx_reads=v.reads);rows.append(row)
 print(scenario,row['passed'],error,[(c['name'],c['actual'],c['expected']) for c in checks if not c['passed']],flush=True)

for scenario in ['Controller reset failure retains active source','DWT counter wrap during cancellation']:
 v=Model();checks=[];error=None
 def step(op,arg=0):return v.step(op,arg)['g_observation']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def irq():
  v.levels()
  if v.rd(GINT)&v.rd(MASK):step(3)
 try:
  step(0,2);eq('real source submit',step(1)['result'],1)
  if scenario.startswith('Controller'):
   v.hold_reset=True;x=step(11);eq('failed DeInit',x['result'],0);eq('old Buffer retained',x['held'],1);eq('old BUSY retained',x['busy'],1);eq('new session denied',step(12)['result'],0)
  else:
   v.wr(0xe0001004,0xffff0000);step(2)
   for i in range(24):
    irq();x=step(2)
    if x['phase']==5:break
   eq('success across CYCCNT wrap',x['result'],1);eq('source retired',x['held'],0)
 except Exception as e:error=str(e)
 row=dict(case=len(rows)+1,name=scenario,passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,trace=v.trace,register_writes=v.writes,rx_reads=v.reads);rows.append(row);print(scenario,row['passed'],error,flush=True)
v=Model();checks=[];error=None
def step(op,arg=0):return v.step(op,arg)['g_observation']
def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
def irq():
 v.levels()
 if v.rd(GINT)&v.rd(MASK):step(3)
try:
 step(0,0x82);step(1);v.hold_flush=True;step(2)
 for i in range(24):
  irq();x=step(2)
  if x['phase']==3:break
 eq('target flush pending',x['phase'],3)
 v.wr(BASE+0x918,64);v.wr(BASE+0x81c,v.rd(BASE+0x81c)|1)
 start=len(v.writes);eq('EP0 IN submission retained',step(13)['result'],1)
 eq('no FIFO0 PIO write during flush',sum(w['address']==hex(BASE+0x1000) for w in v.writes[start:]),0)
 eq('EP0 TXFE masked during flush',v.rd(BASE+0x834)&1,0)
 v.hold_flush=False;x=step(2);eq('target abort SUCCESS',x['result'],1)
 eq('EP0 TXFE resumed after flush',v.rd(BASE+0x834)&1,1)
 # Hardware TXFE is masked per-endpoint, independent of common DIEPMSK.
 v.wr(BASE+0x908,v.rd(BASE+0x908)|128);v.wr(IMASK,v.rd(IMASK)|128);irq()
 eq('EP0 PIO writes resumed after command',sum(w['address']==hex(BASE+0x1000) for w in v.writes[start:])>0,True)
 eq('EP0 source not faulted',step(4)['device_fault'],0)
except Exception as e:error=str(e)
rows.append(dict(case=len(rows)+1,name='shared TX flush fences other EP0 PIO producer',passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,trace=v.trace,register_writes=v.writes,rx_reads=v.reads));print(rows[-1]['name'],rows[-1]['passed'],error,flush=True)
for is_write in (False,True):
 v=Model();checks=[];error=None
 def step(op,arg=0):return v.step(op,arg)['g_observation']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def irq():
  v.levels()
  if v.rd(GINT)&v.rd(MASK):step(3)
 try:
  step(0,2 if is_write else 0x82);step(9)
  cdb=bytes([0x2a if is_write else 0x28,0,0,0,0,0,0,0,1,0])+b'\0'*6
  v.queue(2,struct.pack('<IIIBBB16s',0x43425355,77,512,0 if is_write else 0x80,0,10,cdb));v.queue(2,kind=3);v.complete(2);irq();step(8)
  if not is_write:step(10);step(8);v.hold_in=True
  else:v.auto_nak=False
  v.queue(0,bytes([0x21,0xff,0,0,0,0,0,0]),kind=6);v.queue(0,kind=4);irq();x=step(8)
  eq('PENDING no Status ZLP',x['status_arms'],0);eq('PENDING holds source',x['held'],1)
  for i in range(8):irq();step(8)
  step(4,51);x=step(8)
  eq('timeout is failure',x['device_fault'],1);eq('FAILED holds source',x['held'],1);eq('FAILED no Status ZLP',x['status_arms'],0);eq('no fresh transfer after FAILED',step(5)['result'],0)
 except Exception as e:error=str(e)
 rows.append(dict(case=len(rows)+1,name='real MSC '+('OUT missing NAK' if is_write else 'IN missing Disable')+' Reset failure retains buffer and status',passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,trace=v.trace,register_writes=v.writes,rx_reads=v.reads));print(rows[-1]['name'],rows[-1]['passed'],error,flush=True)
report=dict(cases=rows,summary=dict(cases=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['checks']) for x in rows)),sources={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files+['ThirdParty/tinyusb/src/device/usbd.c','ThirdParty/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c']},commands=commands,artifact=str(axf),hardware='NOT_TESTED',boundary='Real ARM DCD/Core; hardware MMIO transitions only, no software proof or ownership mutation by model')
prior.write_text(json.dumps(report,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in rows))
