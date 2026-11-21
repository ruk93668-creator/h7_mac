"""R2 ResetClosure R1: complete Keil AXF, native FreeRTOS queue and real DWC2.
Only hardware events, queue input and explicit CPU preemption are modeled.
No software SUCCESS, busy, proof or buffer pointer is manufactured.
"""
from pathlib import Path
import json,hashlib,struct
HERE=Path(__file__).resolve().parent
exec((HERE/'run_r2_reset_tests.py').read_text(encoding='utf-8').split('\nrows=[]')[0],globals())

# Observe the compiled native queue layout, never write its count/proof fields.
with axf.open('rb') as image:
 dwarf=ELFFile(image).get_dwarf_info()
 fields={}
 for cu in dwarf.iter_CUs():
  for die in cu.iter_DIEs():
   if die.tag=='DW_TAG_structure_type' and die.attributes.get('DW_AT_name') and die.attributes['DW_AT_name'].value==b'QueueDefinition':
    for member in die.iter_children():
     if member.tag=='DW_TAG_member' and 'DW_AT_name' in member.attributes and 'DW_AT_data_member_location' in member.attributes:
      value=member.attributes['DW_AT_data_member_location'].value
      if isinstance(value,int):fields[member.attributes['DW_AT_name'].value.decode()]=value
      else:
       from elftools.dwarf.dwarf_expr import DWARFExprParser
       ops=DWARFExprParser(dwarf.structs).parse_expr(value)
       assert len(ops)==1 and ops[0].op_name=='DW_OP_plus_uconst'
       fields[member.attributes['DW_AT_name'].value.decode()]=ops[0].args[0]
 assert 'uxMessagesWaiting' in fields and 'uxLength' in fields and 'uxItemSize' in fields,fields

class R1Model(ResetModel):
 def __init__(self):
  super().__init__();self.sends=[];self.resets=[];self.publications=[]
  def audit(c,a,n,ctx):
   if a==(symbols['mscd_reset']&~1):
    self.resets.append(dict(generation=self.rd(symbols['reset_generation']),basepri=c.reg_read(UC_ARM_REG_BASEPRI),phase=self.phase()))
   if a==(symbols['xQueueGenericSend']&~1) and c.reg_read(UC_ARM_REG_R0)==self.rd(symbols['_usbd_q']):
    pointer=c.reg_read(UC_ARM_REG_R1)
    self.sends.append(dict(event=self.cpu.mem_read(pointer+1,1)[0],timeout=c.reg_read(UC_ARM_REG_R2),basepri=c.reg_read(UC_ARM_REG_BASEPRI)))
   if a in [(symbols.get(n,0)&~1) for n in ['xQueueGenericSend','xQueueGenericSendFromISR']] and c.reg_read(UC_ARM_REG_R0)==self.rd(symbols['_usbd_q']):
    pointer=c.reg_read(UC_ARM_REG_R1);eid=self.cpu.mem_read(pointer+1,1)[0]
    if eid in (1,2):self.publications.append(dict(event=eid,generation=self.rd(pointer+12),queue_before=self.count(),isr=c.reg_read(UC_ARM_REG_IPSR)!=0))
  self.cpu.hook_add(UC_HOOK_CODE,audit)
 def count(self):
  q=self.rd(symbols['_usbd_q']);assert self.rd(q+fields['uxLength'])==32 and self.rd(q+fields['uxItemSize'])==20
  return self.rd(q+fields['uxMessagesWaiting'])
 def fill(self,target,kind=8):
  event=bytearray(20);event[1]=kind;event[8]=2
  struct.pack_into('<I',event,12,31);struct.pack_into('<I',event,16,self.call('dcd_edpt_generation',0,2))
  if kind==9:struct.pack_into('<II',event,8,symbols['TinyUSB_RamPoll'],0)
  self.cpu.mem_write(0x2001d200,bytes(event))
  while self.count()<target:
   assert self.call('xQueueGenericSend',self.rd(symbols['_usbd_q']),0x2001d200,0,0)==1
  assert self.count()==target
 def drive(self,limit=60):
  for _ in range(limit):
   self.step_reset()
   if self.call('usbd_device_failed'):return False
   if not self.phase() and not self.cpu.mem_read(symbols['reset_retiring'],1)[0]:return True
   if self.cpu.mem_read(symbols['reset_progress']+5,1)[0] and not self.cpu.mem_read(symbols['reset_progress']+4,1)[0]:self.enum()
  return False
 def end_ready(self):
  self.begin()
  for _ in range(32):
   if self.phase()==6:return
   self.step_reset()
   if self.cpu.mem_read(symbols['reset_progress']+5,1)[0] and not self.cpu.mem_read(symbols['reset_progress']+4,1)[0]:self.enum()
  raise RuntimeError('hardware did not reach NAK release')
 def qualified_setup(self,data):
  assert self.cpu.mem_read(symbols['reset_progress']+4,1)[0]
  self.queue(0,data,kind=6);self.queue(0,kind=4);self.irq()

# Suspend a real ARM Task instruction, run the actual OTG handler in its exception
# context, then resume the untouched Task registers/stack. Hardware/state effects persist.
def preempt(v,entry,action,predicate=lambda c:True,after_entry=False):
 stopped=[False];entered=[False]
 def halt(c,a,n,ctx):
  if not stopped[0] and predicate(c):
   if a==(symbols[entry]&~1):
    if after_entry:entered[0]=True;return
    stopped[0]=True;c.emu_stop()
   elif entered[0]:stopped[0]=True;c.emu_stop()
 hook=v.cpu.hook_add(UC_HOOK_CODE,halt)
 try:action()
 except RuntimeError as e:
  if not stopped[0]:raise
 assert stopped[0],('preemption point not reached',entry)
 saved=v.cpu.context_save();stack=bytes(v.cpu.mem_read(0x2001f000,0x1000))
 assert v.cpu.reg_read(UC_ARM_REG_BASEPRI)==0,('IRQ cannot preempt masked Task',entry)
 v.begin()
 v.cpu.mem_write(0x2001f000,stack);v.cpu.context_restore(saved)
 v.cpu.emu_start(v.cpu.reg_read(UC_ARM_REG_PC)|1,0x01000000,count=1000000)
 assert v.cpu.reg_read(UC_ARM_REG_PC)==0x01000000,'preempted Task failed to return'
 v.cpu.hook_del(hook)

rows=[]
names=['START queue31 accepted','START queue32 retries without fault','END queue31 accepted once','END queue32 retains then retries','END nonretirable full queue bounded fault','START nonretirable full queue retains','duplicate START END exactly one Class Reset','post ENUM descriptor without host retry','post ENUM SET_CONFIGURATION without host retry','post ENUM latest SETUP supersedes previous','post ENUM SETUP under END queue pressure','SETUP retry after queue32','post ENUM setup with missing EPDISD retains','Reset before closure entry','Reset after group proof before Class callback','Reset preempts native Reset Class callback','Reset after END accepted before DCD commit','Reset hardware arrives inside queue short lock','full queue backend completion fails closed','Reset pending preserves queued backend function','SETUP tag wrap keeps native sequence','SETUP sequence exhaustion retains fault','last native SETUP sequence remains valid','malformed post ENUM SETUP fails closed','post ENUM SETUP waits for old EP0 IN Reset proof','new SETUP preemption of restored active IN remains gated','ENUM and SETUP share IRQ snapshot','Reset OUT IRQ does not steal SETUP W1C','unarmed EP0 synthetic completion retires flags']
for name in names:
 v=R1Model();checks=[];error=None
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  board(v);old=v.source(2);original=v.classes();base=len(v.resets);seq=v.call('usbd_setup_sequence')
  if name.startswith('START queue'):
   n=31 if '31' in name else 32;v.fill(n);v.begin()
   eq('IRQ publication never faults',v.call('usbd_device_failed'),0);eq('pending',v.call('dcd_reset_pending',0),1)
   eq('queue exact capacity',v.count(),32);eq('old source held',v.source(2),old);eq('no Class callback',len(v.resets),base)
   eq('START fence increments only once',v.call('usbd_setup_sequence'),seq+1)
   eq('same Task drains and completes',v.drive(),True);eq('exactly one native reset',len(v.resets)-base,1)
   eq('retry does not reissue fence',v.call('usbd_setup_sequence'),seq+1);configure(v);eq('new CBW',v.call('usbd_edpt_busy',0,2),1)
  elif name.startswith('END queue'):
   n=31 if '31' in name else 32;v.end_ready();v.fill(n);v.call('dcd_task',0)
   eq('queue publication returns',v.call('usbd_device_failed'),0)
   if n==32:
    eq('failed publish retains pending',v.phase(),9);eq('failed publish retains source',v.source(2),old);eq('failed publish retains Class',v.classes()==original,True)
    eq('no Class reset',len(v.resets),base);eq('queue unchanged by failed send',v.count(),32)
    v.call('tud_task_ext',0,0);eq('consumer frees bounded old events',v.count(),24);eq('still no Class reset',len(v.resets),base)
   else:eq('accepted END fills queue',v.count(),32);eq('source retired only after acceptance',v.source(2),0)
   eq('native Task completes retry',v.drive(),True);eq('Class reset exactly once',len(v.resets)-base,1)
  elif 'nonretirable' in name or name=='Reset pending preserves queued backend function':
   if name.startswith('END'):v.end_ready();v.fill(32,9);v.call('dcd_task',0)
   else:v.fill(32,9);v.begin()
   eq('queue full retained',v.count(),32);eq('no callback during pending',len(v.resets),base)
   for _ in range(5):v.step_reset()
   eq('no arbitrary function execution',v.count(),32);eq('Buffer retained',v.source(2),old);eq('Class retained',v.classes()==original,True)
   v.wr(symbols['xTickCount'],51 if name.startswith('END') else 1001);v.step_reset();eq('bounded failure',v.call('usbd_device_failed'),1)
   if name.startswith('END'):eq('publication timeout is diagnostic',v.cpu.mem_read(symbols['reset_progress']+2,1)[0],39)
   frozen=v.count()
   for _ in range(3):v.call('tud_task_ext',0,0)
   eq('fault queue evidence remains frozen',v.count(),frozen)
   eq('pending not cleared on failure',v.call('dcd_reset_pending',0),1);eq('old Buffer retained',v.source(2),old);eq('no Class reset on failure',len(v.resets),base)
  elif name.startswith('duplicate'):
   v.begin();g=v.call('dcd_reset_generation',0);eq('duplicate START accepted as metadata',v.call('dcd_event_reset_try',0,0,0,0),1)
   eq('reset completes',v.drive(),True);eq('one reset despite duplicate START',len(v.resets)-base,1)
   eq('duplicate END accepted idempotently',v.call('dcd_event_reset_try',0,1,0,0),1);v.call('tud_task_ext',0,0)
   eq('still one reset',len(v.resets)-base,1);eq('generation not changed',v.call('dcd_reset_generation',0),g)
  elif name.startswith('post ENUM'):
   if 'missing' in name:v.hold_disable.add(2)
   v.begin()
   for _ in range(20):
    v.step_reset()
    if v.phase()==3:break
   v.enum();descriptor=bytes([0x80,6,0,1,0,0,18,0]);v.qualified_setup(bytes([0,9,1,0,0,0,0,0]) if 'SET_CONFIGURATION' in name else descriptor)
   eq('no early new control arm',v.rd(BASE+0x900)&(1<<31),0);eq('old source retained until END',v.source(2),old)
   if 'latest' in name:v.qualified_setup(bytes([0,5,7,0,0,0,0,0]))
   if 'pressure' in name:
    for _ in range(24):
     if v.phase()==6:break
     v.step_reset()
    v.fill(32);v.call('dcd_task',0);eq('END full pending',v.phase(),9);eq('setup storage held',v.cpu.mem_read(symbols['reset_setup_seen'],1)[0],1)
   if 'missing' in name:
    v.drive();v.wr(symbols['xTickCount'],51);v.step_reset();eq('Disable failure bounded',v.call('usbd_device_failed'),1);eq('source retained',v.source(2),old);eq('no Class reset',len(v.resets),base);eq('no control status',v.rd(BASE+0x900)&(1<<31),0)
   else:
    eq('retirement and native events complete',v.drive(),True);v.call('tud_task_ext',0,0)
    eq('one Reset callback',len(v.resets)-base,1)
    if 'SET_CONFIGURATION' in name:
     eq('configuration from single captured SETUP',v.call('usbd_edpt_busy',0,2),1);in_complete(v,0);eq('new INQUIRY',inquiry(v),True)
    elif 'latest' in name:eq('latest SET_ADDRESS accepted once',v.rd(BASE+0x800)>>4&127,7)
    else:
     eq('single captured descriptor GET processed',v.rd(BASE+0x910)&0x7ffff,18);eq('descriptor Data IN live',v.call('usbd_edpt_busy',0,0x80),1)
     in_complete(v,0);out_packet(v,0,b'');configure(v);eq('CBW after saved descriptor',v.call('usbd_edpt_busy',0,2),1)
  elif name=='SETUP retry after queue32':
   v.begin();eq('reset',v.drive(),True);v.fill(32)
   v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=4);v.irq()
   eq('full queue keeps request',v.cpu.mem_read(symbols['reset_setup_seen'],1)[0],1)
   for _ in range(4):v.call('tud_task_ext',0,0)
   eq('request without host retransmit',v.rd(BASE+0x910)&0x7ffff,18);eq('no fault',v.call('usbd_device_failed'),0)
  elif name=='Reset before closure entry':
   v.setup(bytes([0,9,0,0,0,0,0,0]));preempt(v,'usbd_poll_closure',lambda:v.call('tud_task_ext',0,0))
   eq('overlapping hardware scopes fail',v.call('usbd_device_failed'),1);eq('no premature Class reset',len(v.resets),base);eq('old source retained',v.source(2),old)
  elif name=='Reset after group proof before Class callback':
   v.setup(bytes([0,9,0,0,0,0,0,0]))
   # Poll until the one Task reaches the proven old-scope callback; no forced proof.
   preempt(v,'class_reset_drivers',lambda:[v.step_reset() for _ in range(24)])
   eq('new Reset fence survives old closure',v.cpu.mem_read(symbols['reset_retiring'],1)[0],1);eq('old Status never submitted',v.rd(BASE+0x900)&(1<<31),0)
   eq('new Reset can finish',v.drive(),True);eq('one group callback and one Bus Reset',len(v.resets)-base,2);configure(v);eq('new CBW',v.call('usbd_edpt_busy',0,2),1)
  elif name=='Reset preempts native Reset Class callback':
   v.begin();preempt(v,'mscd_reset',lambda:v.drive(),after_entry=True)
   eq('new generation pending',v.call('dcd_reset_pending',0),1);eq('new fence retained',v.cpu.mem_read(symbols['reset_retiring'],1)[0],1)
   eq('new generation completes',v.drive(),True);gs=[x['generation'] for x in v.resets[base:]]
   eq('two generations each reset once',len(gs)==2 and len(set(gs))==2,True);configure(v);eq('CBW',v.call('usbd_edpt_busy',0,2),1)
  elif name=='Reset after END accepted before DCD commit':
   v.end_ready();preempt(v,'tud_event_hook_cb',lambda:v.call('dcd_task',0),lambda c:c.reg_read(UC_ARM_REG_R1)==2)
   eq('invalidated proof fails closed',v.call('usbd_device_failed'),1);eq('old source not released',v.source(2),old);eq('Class never reset from queued END',len(v.resets),base)
   frozen=v.count();v.call('tud_task_ext',0,0);eq('queued END cannot reset on fault',len(v.resets),base);eq('fault retains queued END evidence',v.count(),frozen)
  elif name=='Reset hardware arrives inside queue short lock':
   v.end_ready();injected=[False]
   def raw_reset(c,a,n,ctx):
    if not injected[0] and a==(symbols['xQueueGenericSend']&~1) and c.reg_read(UC_ARM_REG_R0)==v.rd(symbols['_usbd_q']):
     ptr=c.reg_read(UC_ARM_REG_R1)
     if v.cpu.mem_read(ptr+1,1)[0]==2:
      eq('IRQ masked only in publication window',c.reg_read(UC_ARM_REG_BASEPRI),0x50);v.wr(GINT,v.rd(GINT)|(1<<12));injected[0]=True
   v.cpu.hook_add(UC_HOOK_CODE,raw_reset);v.call('dcd_task',0)
   eq('actual event injected',injected[0],True);eq('raw Reset denies retirement',v.call('usbd_device_failed'),1);eq('source retained',v.source(2),old);eq('Class not reset',len(v.resets),base)
  elif name=='SETUP tag wrap keeps native sequence':
   v.begin();eq('initial reset completes',v.drive(),True);v.fill(31)
   v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=4);v.irq();eq('old SETUP queues at tail',v.count(),32)
   before=len(v.writes);sequence=v.call('usbd_setup_sequence')
   for i in range(256):
    v.queue(0,bytes([0,5,7,0,0,0,0,0]),kind=6);v.queue(0,kind=4);v.irq()
   eq('native sequence advances despite queue full',v.call('usbd_setup_sequence'),sequence+256)
   for _ in range(5):v.call('tud_task_ext',0,0)
   eq('latest SET_ADDRESS executed',v.rd(BASE+0x800)>>4&127,7)
   eq('wrapping tag never revives old descriptor',any(w['address']==hex(BASE+0x1000) for w in v.writes[before:]),False)
  elif name=='SETUP sequence exhaustion retains fault':
   # Exhaustion is a diagnostic identity-fault input, not a fabricated stop proof.
   v.wr(symbols['_usbd_setup_seq'],0xffffffff)
   v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=4);v.irq()
   eq('exhaustion fails instead of wrapping',v.call('usbd_device_failed'),1);eq('old source retained',v.source(2),old);eq('no Class reset',len(v.resets),base)
  elif name=='last native SETUP sequence remains valid':
   v.wr(symbols['_usbd_setup_seq'],0xfffffffe)
   v.setup(bytes([0x80,6,0,1,0,0,18,0]))
   eq('last sequence assigned without wrap',v.call('usbd_setup_sequence'),0xffffffff)
   eq('last descriptor delivered',v.rd(BASE+0x910)&0x7ffff,18);eq('no early exhaustion fault',v.call('usbd_device_failed'),0)
  elif name=='malformed post ENUM SETUP fails closed':
   v.begin()
   for _ in range(20):
    v.step_reset()
    if v.phase()==3:break
   v.enum();v.queue(0,b'1234',kind=6);v.irq()
   eq('bad RX status fails rather than inventing request',v.call('usbd_device_failed'),1);eq('old buffer held',v.source(2),old);eq('no Class reset',len(v.resets),base)
  elif name=='post ENUM SETUP waits for old EP0 IN Reset proof':
   v.setup(bytes([0x80,6,0,1,0,0,18,0]));old_in=v.source(0x80);generation=v.call('dcd_edpt_generation',0,0x80)
   v.begin()
   for _ in range(20):
    v.step_reset()
    if v.phase()==3:break
   v.enum();v.qualified_setup(bytes([0,5,7,0,0,0,0,0]))
   eq('old IN source remains owned before Reset proof',v.source(0x80),old_in)
   eq('no new status arm before proof',v.call('dcd_edpt_generation',0,0x80),generation)
   eq('explicit Reset IN proof and queued control succeed',v.drive(),True);v.call('tud_task_ext',0,0)
   eq('captured SET_ADDRESS without retry',v.rd(BASE+0x800)>>4&127,7);eq('one Class Reset',len(v.resets)-base,1)
  elif name=='new SETUP preemption of restored active IN remains gated':
   v.begin()
   for _ in range(20):
    v.step_reset()
    if v.phase()==3:break
   v.enum();v.qualified_setup(bytes([0x80,6,0,1,0,0,18,0]));eq('first captured setup succeeds',v.drive(),True);v.call('tud_task_ext',0,0)
   old_in=v.source(0x80);generation=v.call('dcd_edpt_generation',0,0x80)
   eq('restored IN is active',v.rd(BASE+0x900)&(1<<31),1<<31)
   v.queue(0,bytes([0,5,7,0,0,0,0,0]),kind=6);v.queue(0,kind=4);v.irq()
   eq('unproven legacy ISR abort stays faulted',v.call('usbd_device_failed'),1)
   eq('old control source retained',v.source(0x80),old_in);eq('no new Status ZLP',v.call('dcd_edpt_generation',0,0x80),generation)
  elif name=='ENUM and SETUP share IRQ snapshot':
   v.begin()
   for _ in range(20):
    v.step_reset()
    if v.phase()==3:break
   v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=4)
   v.wr(GINT,v.rd(GINT)|(1<<13));v.irq()
   eq('same snapshot preserves SETUP pair',v.cpu.mem_read(symbols['reset_setup_done'],1)[0],1)
   eq('old source held before proof',v.source(2),old);eq('Reset END and captured request complete',v.drive(),True);v.call('tud_task_ext',0,0)
   eq('descriptor delivered without second SETUP',v.rd(BASE+0x910)&0x7ffff,18);eq('one Class Reset',len(v.resets)-base,1)
  elif name=='Reset OUT IRQ does not steal SETUP W1C':
   v.begin()
   for _ in range(20):
    v.step_reset()
    if v.phase()==3:break
   v.enum();done=[False];early=[]
   def pop(c,access,a,n,value,ctx):
    if ((v.rd(a)>>17)&15)==4:done[0]=True
   def w1c(c,access,a,n,value,ctx):
    if value&8 and not done[0]:early.append(value)
   v.cpu.hook_add(UC_HOOK_MEM_READ,pop,begin=BASE+0x20,end=BASE+0x23)
   v.cpu.hook_add(UC_HOOK_MEM_WRITE,w1c,begin=BASE+0xb08,end=BASE+0xb0b)
   v.queue(0,bytes([0x80,6,0,1,0,0,18,0]),kind=6);v.queue(0,kind=3);v.queue(0,kind=4)
   v.wr(BASE+0xb08,8|1|(1<<15));v.irq()
   eq('no SETUP W1C before its destructive DONE pop',early,[]);eq('actual DONE consumed',done[0],True)
   eq('Reset and saved control complete',v.drive(),True);v.call('tud_task_ext',0,0);eq('descriptor from one request',v.rd(BASE+0x910)&0x7ffff,18)
  elif name=='unarmed EP0 synthetic completion retires flags':
   before=v.call('dcd_edpt_generation',0,0)
   v.wr(BASE+0xb08,1|(1<<5));v.queue(0,kind=3);v.irq();v.call('tud_task_ext',0,0)
   eq('synthetic flags cleared by RX consumer',v.rd(BASE+0xb08)&(1|(1<<5)),0)
   eq('no false OUT transfer generation',v.call('dcd_edpt_generation',0,0),before)
   eq('CBW stays owned',v.call('usbd_edpt_busy',0,2),1);eq('no fault',v.call('usbd_device_failed'),0)
  elif name=='full queue backend completion fails closed':
   command(v,0x28,False);v.fill(32,9);v.begin();v.call('TinyUSB_RamPoll')
   eq('enqueue failure latches fault',v.call('usbd_device_failed'),1);eq('MSC ownership retained',v.call('mscd_io_quiescent'),0);eq('Class not reset',len(v.resets),base)
  generations={x['generation'] for x in v.publications}
  ordered=True
  for g in generations:
   events=[x['event'] for x in v.publications if x['generation']==g]
   if 2 in events:ordered=ordered and 1 in events and events.index(1)<events.index(2)
  eq('native START precedes END publication',ordered,True)
  eq('Task native queue sends use zero wait',all(x['timeout']==0 for x in v.sends),True)
  eq('Class callbacks outside critical region',all(x['basepri']==0 for x in v.resets),True)
 except Exception as e:error=str(e)
 row=dict(name=name,passed=not error and all(x['passed'] for x in checks),checks=checks,error=error,calls=v.calls,queue_sends=v.sends,reset_publications=v.publications,class_callbacks=v.resets,phase=v.phase(),failure=v.cpu.mem_read(symbols['reset_progress']+2,1)[0],register_writes=v.writes);rows.append(row)
 print(name,row['passed'],error,[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],flush=True)
report=dict(baseline='f4dada109a9302a5eab66bcad8e2d7ad82a16738',artifact=str(axf),axf_sha256=hashlib.sha256(axf.read_bytes()).hexdigest(),summary=dict(cases=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['checks']) for x in rows)),native_queue_layout=fields,cases=rows,hardware='NOT_TESTED')
p=HERE/'reports/r2-reset-r1.json'
if p.exists():(HERE/'reports'/('r2-reset-r1-history-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(p.read_bytes())
p.write_text(json.dumps(report,indent=2),encoding='utf-8');raise SystemExit(not all(x['passed'] for x in rows))
