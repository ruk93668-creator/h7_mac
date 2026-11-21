"""Execute the complete Keil AXF, actual FreeRTOS port/queues and TinyUSB; no fake DCD.
Scheduler/NVIC readiness is an explicit environmental input, not a board claim.
"""
from pathlib import Path
import json,struct,sys,hashlib,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
sys.path.insert(0,str(ROOT/'Tests/UsbBulkOutLifecycle'))
from harness import VM,BASE,GINT,MASK,DCTL,IMASK
from elftools.elf.elffile import ELFFile
from unicorn import UC_HOOK_MEM_READ,UC_HOOK_CODE
from unicorn.arm_const import *
build=json.loads((HERE/'reports/r2-build.json').read_text(encoding='utf-8'));axf=Path(build['output'])/'h7_mac_CM7_TinyUSB_RAM.axf'
for path,h in build['source_sha256'].items():assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==h,('stale AXF',path)
with axf.open('rb') as f:
 e=ELFFile(f);load_segments=[(p['p_paddr'],p.data()) for p in e.iter_segments() if p['p_type']=='PT_LOAD' and p['p_filesz']];symbols={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
class Model(VM):
 def __init__(self):
  super().__init__(symbols,sections,'tud_rhport_init',{})
  # ARMCC scatter images may compress RW initializers in the Flash load segment.
  # Load real LMA bytes and run the actual library scatter/decompress code before
  # the modeled scheduler-ready state. Never manufacture initialized USB state.
  for address,data in load_segments:self.cpu.mem_write(address,data)
  self.cpu.reg_write(UC_ARM_REG_SP,0x2001fff0)
  self.cpu.emu_start(symbols['__scatterload']|1,symbols['__rt_entry']&~1,count=1000000)
  assert self.cpu.reg_read(UC_ARM_REG_PC)==(symbols['__rt_entry']&~1),'scatter initialization did not finish'

  self.cpu.mem_map(0x58000000,0x100000)
  self.cpu.hook_add(UC_HOOK_MEM_READ,self.clock,begin=0xe0001004,end=0xe0001007)
  self.wr(BASE+0x808,3<<1) # FS48 PHY negotiated speed (DSTS hardware input)
  self.wr(BASE+0x48,8<<10);self.wr(BASE+0x40,0x4f54310a);self.wr(0xe000ed88,0xf00000)
  self.wr(symbols['SystemCoreClock'],480000000);self.wr(0xe0001000,1);self.wr(0xe000edfc,1<<24)
  # State normally set by xPortStartScheduler. It is input to this software model.
  self.wr(symbols['uxCriticalNesting'],0);self.cpu.mem_write(symbols['ucMaxSysCallPriority'],b'\x50');self.wr(symbols['ulMaxPRIGROUPValue'],0x300)
  self.cpu.mem_write(0xe000e400+101,b'\x50');self.wr(0xe000ed0c,0x300)
  self.wr(0x2001d000,1);self.wr(0x2001d004,0)
  self.calls=[];self.hold_reset=False;self.stall_clock=False
  self.wr(BASE+0x918,256);self.wr(BASE+0x958,256)
 def clock(self,cpu,access,a,size,v,ctx):
  if not self.stall_clock:self.wr(a,(self.rd(a)+48000)&0xffffffff)
 def read(self,cpu,access,a,size,v,ctx):
  if a==BASE+0x10:
   if self.hold_reset:return
   self.wr(a,self.rd(a)&~1)
  if a==BASE+0x20 and self.q:
   status=self.q[0][0];ep=status&15;n=(status>>4)&0x7ff
   if (status>>17)&15==2:
    size=self.rd(BASE+0xb10+32*ep);self.wr(BASE+0xb10+32*ep,(size&~0x7ffff)|max(0,(size&0x7ffff)-n))
  if a==BASE+0x940:
   value=self.rd(a)
   if value&(1<<27):self.wr(a+8,self.rd(a+8)|(1<<6))
   if value&(1<<30):self.wr(a,value&~((1<<30)|(1<<31)));self.wr(a+8,self.rd(a+8)|2)
  super().read(cpu,access,a,size,v,ctx)
 def call(self,name,*args,isr=False):
  c=self.cpu;c.reg_write(UC_ARM_REG_SP,0x2001fff0);c.reg_write(UC_ARM_REG_LR,0x01000001)
  for r,v in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):c.reg_write(r,v)
  # A 5th C argument lives on the stack (usbd_edpt_xfer is task-only here).
  self.wr(0x2001fff0,0)
  c.reg_write(UC_ARM_REG_IPSR,117 if isr else 0);self.wr(0xe000ed04,117 if isr else 0)
  c.emu_start(symbols[name]|1,0x01000000,count=1000000)
  returned=c.reg_read(UC_ARM_REG_PC)==0x01000000
  self.calls.append(dict(function=name,args=list(args),returned=returned,basepri=c.reg_read(UC_ARM_REG_BASEPRI)))
  c.reg_write(UC_ARM_REG_IPSR,0);self.wr(0xe000ed04,0)
  if not returned:raise RuntimeError(name+' did not return; PC='+hex(c.reg_read(UC_ARM_REG_PC)))
  return c.reg_read(UC_ARM_REG_R0)
 def irq(self):
  self.levels()
  if self.rd(GINT)&self.rd(MASK):return self.call('OTG_FS_IRQHandler',isr=True)
 def init(self):return self.call('tud_rhport_init',0,0x2001d000)
 def reset(self):
  self.wr(GINT,self.rd(GINT)|(1<<12));self.irq()
  self.wr(GINT,self.rd(GINT)|(1<<13));self.irq();self.call('tud_task_ext',0,0)
 def setup(self,data):
  self.queue(0,data,kind=6);self.queue(0,kind=4);self.irq();return self.call('tud_task_ext',0,0)
 def open(self,ep):
  self.cpu.mem_write(0x2001d100,bytes([7,5,ep,2,64,0,0]));return self.call('usbd_edpt_open',0,0x2001d100)
def in_complete(v,ep):
 v.wr(BASE+0x900+32*ep,v.rd(BASE+0x900+32*ep)&~(1<<31))
 v.wr(BASE+0x910+32*ep,0);v.wr(BASE+0x908+32*ep,v.rd(BASE+0x908+32*ep)|1)
 v.irq();v.call('tud_task_ext',0,0)
def out_packet(v,ep,data):
 v.queue(ep,data);v.queue(ep,kind=3);v.complete(ep);v.irq();v.call('tud_task_ext',0,0)
def bulk_bytes(v,start):
 return b''.join(struct.pack('<I',int(w['value'],16)) for w in v.writes[start:] if w['address']==hex(BASE+0x3000))
rows=[]
for case in ['complete image unique owner','FreeRTOS static queue and mutex','nested spin BASEPRI and IRQ admission','empty reset EP0 then configuration','Bulk OUT cancel and Controller restart','active Bulk Bus Reset boundary','active Disconnect boundary','DWT unavailable retains Buffer','RTOS tick deadline during CPU sleep','close already retired group','close active group preserves fault','RAM backend asynchronous completion']:
 v=Model();checks=[];error=None
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  eq('real full firmware init',v.init(),1)
  if case.startswith('complete'):
   eq('strong handler exists','OTG_FS_IRQHandler' in symbols,True)
   for n in ['HAL_PCD_IRQHandler','USB_Mode_Start','USBD_Init','BSP_USB_Start','StorageTask_Run']:eq(n+' absent',n in symbols,False)
   eq('original static task linked','UsbTask_Create' in symbols,True)
  elif case.startswith('FreeRTOS'):
   eq('static queue nonnull',v.rd(symbols['_usbd_q'])!=0,True);eq('static mutex nonnull',v.rd(symbols['_usbd_mutex'])!=0,True)
   for n in ['xQueueGenericCreateStatic','xQueueCreateMutexStatic','vPortEnterCritical']:eq('real kernel '+n,n in symbols,True)
  elif case.startswith('nested'):
   v.call('usbd_spin_lock',0);eq('task BASEPRI',v.cpu.reg_read(UC_ARM_REG_BASEPRI),0x50)
   v.call('usbd_spin_lock',0);eq('nested BASEPRI',v.cpu.reg_read(UC_ARM_REG_BASEPRI),0x50)
   # IRQ pending at encoded priority0x50 cannot preempt this critical window.
   irq_priority=v.cpu.mem_read(0xe000e400+101,1)[0]
   eq('pending USB IRQ blocked by actual BASEPRI',irq_priority>=v.cpu.reg_read(UC_ARM_REG_BASEPRI),True)
   v.call('usbd_spin_unlock',0);eq('inner exit preserves mask',v.cpu.reg_read(UC_ARM_REG_BASEPRI),0x50)
   v.call('usbd_spin_unlock',0);eq('outer exit restores mask',v.cpu.reg_read(UC_ARM_REG_BASEPRI),0)
   v.call('usbd_spin_lock',1,isr=True);eq('ISR spin is nonblocking',v.cpu.reg_read(UC_ARM_REG_BASEPRI),0);v.call('usbd_spin_unlock',1,isr=True)
  elif case.startswith('empty'):
   v.reset();eq('reset without fault',v.call('usbd_device_failed'),0)
   v.setup(bytes([0,9,1,0,0,0,0,0]));eq('configuration armed a generation',v.call('dcd_edpt_generation',0,2)>0,True);eq('new CBW armed',v.call('usbd_edpt_busy',0,2),1)
  elif case.startswith('RAM'):
   v.reset();v.setup(bytes([0,9,1,0,0,0,0,0]))
   cdb=bytes([0x28,0,0,0,0,0,0,0,1,0])+b'\0'*6
   v.queue(2,struct.pack('<IIIBBB16s',0x43425355,78,512,0x80,0,10,cdb));v.queue(2,kind=3);v.complete(2);v.irq();v.call('tud_task_ext',0,0)
   eq('RAM source pending',v.call('TinyUSB_RamDrained'),0);v.call('TinyUSB_RamPoll');eq('RAM completion drained',v.call('TinyUSB_RamDrained'),1);v.call('tud_task_ext',0,0)
   eq('Data IN armed',v.call('usbd_edpt_busy',0,0x82),1)
  else:
   v.reset();eq('EP2 open',v.open(2),1);eq('real RX arm',v.call('usbd_edpt_xfer',0,2,0x2001e000,64),1)
   if case.startswith('active Disconnect'):
    v.wr(BASE+4,4);v.wr(GINT,v.rd(GINT)|4);v.irq()
    eq('unproven disconnect retains fault',v.call('usbd_device_failed'),1);eq('old BUSY retained',v.call('usbd_edpt_busy',0,2),1)
   elif case.startswith('active'):
    v.wr(GINT,v.rd(GINT)|(1<<12));v.irq()
    eq('unproven active reset stays faulted',v.call('usbd_device_failed'),1);eq('source held',v.rd(symbols['xfer_status']+2*32),0x2001e000)
   elif case.startswith('RTOS tick'):
    eq('PENDING',v.call('usbd_edpt_abort',0,2),0);v.wr(symbols['xTickCount'],51)
    eq('wall tick deadline FAILED',v.call('usbd_edpt_abort',0,2),0);eq('fault',v.call('usbd_device_failed'),1);eq('BUSY retained',v.call('usbd_edpt_busy',0,2),1)
   elif case.startswith('DWT'):
    v.wr(0xe0001000,0);eq('failed abort',v.call('usbd_edpt_abort',0,2),0);eq('fault',v.call('usbd_device_failed'),1);eq('BUSY retained',v.call('usbd_edpt_busy',0,2),1)
   elif case.startswith('close active'):
    v.call('dcd_edpt_close_all',0);eq('fault retained',v.call('usbd_device_failed'),1);eq('busy retained',v.call('usbd_edpt_busy',0,2),1)
   else:
    eq('PENDING retains busy',v.call('usbd_edpt_abort',0,2),0);eq('PENDING DeInit refused',v.call('tud_deinit',0),0)
    for i in range(24):
     v.irq();done=v.call('usbd_edpt_abort',0,2)
     if done:break
    eq('actual cancel SUCCESS',done,1);eq('BUSY cleared',v.call('usbd_edpt_busy',0,2),0)
    if case.startswith('close already'):
     for n in range(1,9):v.wr(BASE+0x908+32*n,128) # hardware read-only TXFE reset level
     v.call('dcd_edpt_close_all',0);eq('retired group close succeeds',v.call('usbd_device_failed'),0)
    else:
     eq('actual Controller DeInit',v.call('tud_deinit',0),1);eq('real restart',v.init(),1)
 except Exception as e:error=str(e)
 rows.append(dict(name=case,passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,calls=v.calls,register_writes=v.writes))
 print(case,rows[-1]['passed'],error,[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],flush=True)
for name in ['SCSI INQUIRY','SCSI TEST UNIT READY','SCSI READ CAPACITY','SCSI REQUEST SENSE','SCSI RAM WRITE then READ','GET_MAX_LUN and BOT Reset','EP0 descriptors and SET_ADDRESS']:
 v=Model();checks=[];error=None
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  eq('init',v.init(),1);v.reset()
  if name.startswith('EP0'):
   start=len(v.writes);v.setup(bytes([0x80,6,0,1,0,0,18,0]));data=b''.join(struct.pack('<I',int(w['value'],16)) for w in v.writes[start:] if w['address']==hex(BASE+0x1000))
   eq('actual device descriptor',list(data[:2]),[18,1]);in_complete(v,0)
   out_packet(v,0,b'');v.setup(bytes([0,5,7,0,0,0,0,0]));eq('address register',v.rd(BASE+0x800)>>4&127,7)
  else:
   v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0)
   eq('configured CBW',v.call('usbd_edpt_busy',0,2),1)
   if name.startswith('GET'):
    v.setup(bytes([0xa1,0xfe,0,0,0,0,1,0]));eq('GET_MAX_LUN Data IN',v.rd(BASE+0x910)&0x7ffff,1);in_complete(v,0);out_packet(v,0,b'')
    v.setup(bytes([0x21,0xff,0,0,0,0,0,0]));eq('BOT Reset status ZLP',v.rd(BASE+0x910)&0x7ffff,0);eq('CBW stays armed',v.call('usbd_edpt_busy',0,2),1)
    in_complete(v,0);v.setup(bytes([2,1,0,0,2,0,0,0]));eq('Clear HALT no fault',v.call('usbd_device_failed'),0);eq('Clear HALT preserves CBW',v.call('usbd_edpt_busy',0,2),1)
   else:
    op=0x12 if 'INQUIRY' in name else 0 if 'READY' in name else 0x25 if 'CAPACITY' in name else 3 if 'SENSE' in name else 0x2a
    size={0x12:36,0:0,0x25:8,3:18,0x2a:512}[op]
    cdb=bytearray(16);cdb[0]=op
    if op in (0x12,3):cdb[4]=size
    if op==0x2a:cdb[8]=1
    start=len(v.writes)
    out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,81,size,0 if op==0x2a else 0x80,0,10 if op in (0x25,0x2a) else 6,bytes(cdb)))
    if op==0x2a:
     out_packet(v,2,bytes([0x39])*512);eq('write backend pending',v.call('TinyUSB_RamDrained'),0);v.call('TinyUSB_RamPoll');v.call('tud_task_ext',0,0)
    elif size:in_complete(v,2)
    data=bulk_bytes(v,start)
    eq('CSW signature/tag',list(data[-16:-8]) if len(data)%4==0 else [],list(struct.pack('<II',0x53425355,81)))
    # CSW is 13 bytes padded only by FIFO word writes; inspect residue/status.
    eq('CSW residue',struct.unpack('<I',data[-8:-4])[0],0);eq('CSW status',data[-4],0)
    if op==0x12:eq('INQUIRY peripheral',data[0],0)
    if op==0x25:eq('capacity 16 sectors / 512 bytes',list(data[:8]),list(struct.pack('>II',15,512)))
    if op==3:eq('Sense response',data[0]&127,0x70)
    in_complete(v,2)
    if op==0x2a:
     cdb[0]=0x28;start=len(v.writes);out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,82,512,0x80,0,10,bytes(cdb)))
     v.call('TinyUSB_RamPoll');v.call('tud_task_ext',0,0)
     eq('RAM read matches written source',bulk_bytes(v,start)[:512],bytes([0x39])*512)
     # JSON stores a compact comparison, not binary payload.
     checks[-1]['actual']=hashlib.sha256(checks[-1]['actual']).hexdigest();checks[-1]['expected']=hashlib.sha256(checks[-1]['expected']).hexdigest()
 except Exception as e:error=str(e)
 rows.append(dict(name=name,passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,calls=v.calls,register_writes=v.writes));print(name,rows[-1]['passed'],error,[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],flush=True)
report=dict(artifact=str(axf),axf_sha256=hashlib.sha256(axf.read_bytes()).hexdigest(),cases=rows,summary=dict(cases=len(rows),passed=sum(r['passed'] for r in rows),assertions=sum(len(r['checks']) for r in rows)),hardware='NOT_TESTED',boundary='Complete Keil AXF real kernel/port/queue/MSC/DCD; explicit scheduler readiness and MMIO model, not RTOS runtime or silicon acceptance')
p=HERE/'reports/r2-firmware-tests.json'
if p.exists():(HERE/'reports'/('r2-firmware-tests-history-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(p.read_bytes())
p.write_text(json.dumps(report,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in rows))
