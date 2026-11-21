"""Real full AXF Reset tests: hardware reacts only to explicit DCD commands.
USBRST never clears EPENA, FIFO, source pointers or Core/Class ownership here.
"""
from pathlib import Path
import json,hashlib,struct
HERE=Path(__file__).resolve().parent
exec((HERE/'run_r2_firmware_tests.py').read_text(encoding='utf-8').split('rows=[]')[0],globals())
from unicorn import UC_HOOK_MEM_WRITE,UC_HOOK_CODE
from elftools.elf.elffile import ELFFile
with axf.open('rb') as f:sizes={s.name:s['st_size'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}

class ResetModel(Model):
 def __init__(self):
  self.hold_rx=False;self.hold_tx=False;self.hold_in=set();self.flush_log=[];self.class_log=[]
  super().__init__()
  self.nvic=[]
  def nvic_write(c,access,a,n,value,ctx):self.nvic.append(dict(address=hex(a),value=value))
  self.cpu.hook_add(UC_HOOK_MEM_WRITE,nvic_write,begin=0xe000e100,end=0xe000e1ff)
  def observe(c,a,n,ctx):
   if a==(symbols['mscd_reset']&~1):self.class_log.append(dict(basepri=c.reg_read(UC_ARM_REG_BASEPRI),pending=self.phase(),source=self.source(2)))
  self.cpu.hook_add(UC_HOOK_CODE,observe)
 def read(self,c,access,a,n,val,ctx):
  if a==BASE+0x10:
   old=self.rd(a)
   if old&(1<<4) and not self.hold_rx:
    self.flush_log.append(dict(kind='rx_completed',discarded=len(self.q),enum=bool(self.rd(GINT)&(1<<13))))
    self.q.clear();self.payload.clear() # only an explicit successful RXFFLSH changes FIFO
   super().read(c,access,a,n,val,ctx)
   if self.hold_rx:self.wr(a,self.rd(a)|(old&(1<<4)))
   if self.hold_tx:self.wr(a,self.rd(a)|(old&(1<<5)))
   return
  for ep in range(9):
   if a==BASE+0x900+ep*32:
    old=self.rd(a)
    if old&(1<<30) and ep not in self.hold_in:
     self.wr(a,old&~((1<<30)|(1<<31)));self.wr(a+8,self.rd(a+8)|2)
  super().read(c,access,a,n,val,ctx)
 def phase(self):return self.cpu.mem_read(symbols['reset_progress'],1)[0]
 def source(self,ep):return self.rd(symbols['xfer_status']+(ep&15)*32+(16 if ep&128 else 0))
 def classes(self):return bytes(self.cpu.mem_read(symbols['_mscd_itf'],sizes['_mscd_itf']))
 def begin(self):self.wr(GINT,self.rd(GINT)|(1<<12));self.irq()
 def enum(self):self.wr(GINT,self.rd(GINT)|(1<<13));self.irq()
 def step_reset(self):self.irq();self.call('tud_task_ext',0,0)
 def finish(self,limit=40,enumerate=True):
  for i in range(limit):
   self.step_reset()
   if enumerate and self.phase() in (3,4,5,6,7):self.enum();enumerate=False
   if self.call('usbd_device_failed'):return False
   if not self.phase():return True
  return False

def configure(v):v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0)
def board(v):assert v.init()==1;v.reset();configure(v)
def command(v,op,poll=True):
 cdb=bytearray(16);cdb[0]=op;cdb[8]=1
 out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,303,512,0x80 if op==0x28 else 0,0,10,bytes(cdb)))
 if poll:v.call('TinyUSB_RamPoll');v.call('tud_task_ext',0,0)
def inquiry(v):
 start=len(v.writes);cdb=bytes([0x12,0,0,0,36,0])+b'\0'*10
 out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,304,36,0x80,0,6,cdb));in_complete(v,2)
 data=bulk_bytes(v,start);return len(data)>=52 and data[-16:-8]==struct.pack('<II',0x53425355,304) and data[-4]==0
rows=[]
for name in ['active OUT Reset and INQUIRY','active IN Reset and READ','IN OUT simultaneous Reset','EP0 OUT SETUP receiver remains enabled','EP0 IN and Reset','RX leftovers consumed by explicit flush','new SETUP during pending discarded','missing IN EPDISD','missing OUT EPDISD','missing Global NAK','RX Flush stuck','TX Flush stuck','ENUM before RX proof','repeat Reset pending','Reset group close overlap','Backend pending across Reset','late old events','three Reset reconfigurations','Reset Disconnect overlap','DWT stops during Reset','DWT wraps during Reset','RX first attempt stalls then retry succeeds','RX old CBW pending at reset','old Status queued across reset','old Reset END after new Session','old SETUP DONE without fresh RX','EP0 fresh descriptor after Reset','new SETUP arrives during RX flush','IRQ remains enabled across reset','Backend completion queued during Reset','Reset IRQ before Task transfer publication']:
 v=ResetModel();checks=[];error=None
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  board(v)
  if name=='active IN Reset and READ':command(v,0x28)
  elif name=='IN OUT simultaneous Reset':
   eq('peer IN open',v.open(0x81),1);eq('peer IN arm',v.call('usbd_edpt_xfer',0,0x81,0x2001e800,64),1)
  elif name=='EP0 OUT SETUP receiver remains enabled':v.wr(BASE+0xb00,v.rd(BASE+0xb00)|(1<<31)) # physical SETUP receiver, not reset side effect
  elif name=='EP0 IN and Reset':v.setup(bytes([0x80,6,0,1,0,0,18,0]))
  elif name in ['Backend pending across Reset','Backend completion queued during Reset']:command(v,0x28,False);eq('IO owned before reset',v.call('TinyUSB_RamDrained'),0)
  elif name=='Reset group close overlap':v.setup(bytes([0,9,0,0,0,0,0,0]))
  if name=='DWT wraps during Reset':v.wr(0xe0001004,0xffff0000)
  if name=='Reset IRQ before Task transfer publication':
   eq('peer IN open',v.open(0x81),1)
   stopped=[False]
   def suspend(c,a,n,ctx):
    if not stopped[0] and a==(symbols['usbd_spin_lock']&~1) and c.reg_read(UC_ARM_REG_BASEPRI)==0:
     stopped[0]=True;c.emu_stop()
   hook=v.cpu.hook_add(UC_HOOK_CODE,suspend)
   try:v.call('usbd_edpt_xfer',0,0x81,0x2001e800,64)
   except RuntimeError:pass
   eq('Task suspended before lock',stopped[0],True)
   saved=v.cpu.context_save();stack=bytes(v.cpu.mem_read(0x2001f000,0x1000))
   v.begin() # actual OTG handler on its modeled exception context
   v.cpu.mem_write(0x2001f000,stack);v.cpu.context_restore(saved)
   v.cpu.emu_start(v.cpu.reg_read(UC_ARM_REG_PC)|1,0x01000000,count=1000000)
   eq('preempted Task returns',v.cpu.reg_read(UC_ARM_REG_PC),0x01000000)
   eq('submission refused after IRQ',v.cpu.reg_read(UC_ARM_REG_R0),0)
   eq('new source never published',v.source(0x81),0)
   eq('old OUT still busy',v.call('usbd_edpt_busy',0,2),1)
   eq('Reset retires after race',v.finish(),True);configure(v);eq('CBW after race',v.call('usbd_edpt_busy',0,2),1)
   row=dict(name=name,passed=all(x['passed'] for x in checks),checks=checks,error=None,register_writes=v.writes,calls=v.calls,phase=v.phase(),failure=v.cpu.mem_read(symbols['reset_progress']+2,1)[0]);rows.append(row);print(name,row['passed'],flush=True);continue
  old=v.source(2);oldbuffer=bytes(v.cpu.mem_read(old,31)) if old else b'';oldclass=v.classes();oldgen=v.call('dcd_edpt_generation',0,2);before=len(v.writes)
  if name in ['RX leftovers consumed by explicit flush','RX old CBW pending at reset']:v.queue(2,b'Q'*31)
  if name=='old Status queued across reset':
   old_status_gen=v.call('dcd_edpt_generation',0,0x80)
  if name=='RX first attempt stalls then retry succeeds':v.hold_rx=True
  if name=='missing IN EPDISD':
   eq('IN open',v.open(0x81),1);eq('IN arm',v.call('usbd_edpt_xfer',0,0x81,0x2001e800,64),1);v.hold_in.add(1)
  if name=='missing OUT EPDISD':v.hold_disable.add(2)
  if name=='missing Global NAK':v.auto_nak=False
  if name=='RX Flush stuck':v.hold_rx=True
  if name=='TX Flush stuck':v.hold_tx=True
  before=len(v.writes);rx0=v.rx_flushes
  v.begin()
  resetgen=v.call('dcd_reset_generation',0)
  if name=='Reset group close overlap':
   eq('overlap fails conservatively',v.call('usbd_device_failed'),1);eq('source retained',v.source(2),old)
  else:
   eq('Reset pending',v.call('dcd_reset_pending',0),1);eq('old class retained before proof',v.classes()==oldclass,True);eq('source before proof',v.source(2),old)
   was_busy=v.call('usbd_edpt_busy',0,2);v.call('usbd_edpt_clear_stall',0,2);eq('pending Clear HALT preserves BUSY',v.call('usbd_edpt_busy',0,2),was_busy)
   eq('pending Abort returns incomplete',v.call('usbd_edpt_abort',0,2),0);eq('pending Abort does not invent failure',v.call('usbd_device_failed'),0)
   eq('pending denies new OUT',v.call('usbd_edpt_xfer',0,2,0x2001e900,31),0);eq('pending denies DeInit',v.call('tud_deinit',0),0)
   if name=='Backend completion queued during Reset':
    v.call('TinyUSB_RamPoll');eq('queued completion retains Class IO until Task',v.call('TinyUSB_RamDrained'),1)
   if name=='ENUM before RX proof':v.enum()
   if name=='repeat Reset pending':v.begin()
   if name=='Reset Disconnect overlap':v.wr(BASE+4,4);v.wr(GINT,v.rd(GINT)|4);v.irq()
   if name=='DWT stops during Reset':v.stall_clock=True
   if name=='new SETUP arrives during RX flush':v.queue(0,bytes([0,9,1,0,0,0,0,0]),kind=6);v.queue(0,kind=4)
   if name=='RX first attempt stalls then retry succeeds':
    v.wr(symbols['xTickCount'],2);v.step_reset();v.hold_rx=False
   if name=='old Status queued across reset':
    event=bytearray(20);event[1]=8;event[8]=0x80;struct.pack_into('<I',event,16,old_status_gen);v.cpu.mem_write(0x2001d200,bytes(event));v.call('dcd_event_handler',0x2001d200,0)
   if name=='new SETUP during pending discarded':
    v.step_reset();v.queue(0,bytes([0,9,1,0,0,0,0,0]),kind=6);v.queue(0,kind=4);v.irq()
   failed=name in ['missing IN EPDISD','missing OUT EPDISD','missing Global NAK','RX Flush stuck','TX Flush stuck','ENUM before RX proof','repeat Reset pending','Reset Disconnect overlap','DWT stops during Reset']
   done=v.finish(enumerate=name!='RX Flush stuck')
   if failed:
    v.wr(symbols['xTickCount'],51);v.step_reset()
    eq('bounded failure',v.call('usbd_device_failed'),1);eq('original source retained',v.source(2),old);eq('old BUSY retained',v.call('usbd_edpt_busy',0,2),1);eq('class not released',v.classes()==oldclass,True)
    eq('new submission still refused',v.call('usbd_edpt_xfer',0,2,0x2001e900,31),0)
   else:
    eq('retirement complete',done,True);eq('no fault',v.call('usbd_device_failed'),0);eq('old source retired',v.source(2),0)
    eq('Class callback outside BASEPRI',all(x['basepri']==0 for x in v.class_log),True)
    eq('Class callback after hardware proof',all(x['pending']==0 and x['source']==0 for x in v.class_log),True)
    if old:eq('old Buffer never copied after reset fence',bytes(v.cpu.mem_read(old,31))==oldbuffer,True)
    if name=='EP0 fresh descriptor after Reset':
     start=len(v.writes);v.setup(bytes([0x80,6,0,1,0,0,18,0]));eq('fresh descriptor length',v.rd(BASE+0x910)&0x7ffff,18);in_complete(v,0);out_packet(v,0,b'')
    if name=='old SETUP DONE without fresh RX':
     gen=v.call('dcd_edpt_generation',0,0x80);v.queue(0,kind=4);v.irq();v.call('tud_task_ext',0,0);eq('stray DONE cannot submit status',v.call('dcd_edpt_generation',0,0x80),gen)
    configure(v)
    if name=='Backend pending across Reset':
     eq('old backend prevents new CBW',v.call('usbd_edpt_busy',0,2),0);eq('backend still held',v.call('TinyUSB_RamDrained'),0);v.call('TinyUSB_RamPoll');v.call('tud_task_ext',0,0)
    eq('fresh CBW',v.call('usbd_edpt_busy',0,2),1);eq('fresh generation',v.call('dcd_edpt_generation',0,2)>oldgen,True)
    if name=='old Reset END after new Session':
     event=bytearray(20);event[1]=2;event[8]=0;struct.pack_into('<I',event,12,resetgen-1);v.cpu.mem_write(0x2001d200,bytes(event));v.call('dcd_event_handler',0x2001d200,0);v.call('tud_task_ext',0,0);eq('old reset END cannot reset new CBW',v.call('usbd_edpt_busy',0,2),1)
    if name=='late old events':
     event=bytearray(20);event[1]=8;event[8]=2;struct.pack_into('<I',event,12,31);struct.pack_into('<I',event,16,oldgen);v.cpu.mem_write(0x2001d200,bytes(event));v.call('dcd_event_handler',0x2001d200,0);v.call('tud_task_ext',0,0);eq('old event cannot release new CBW',v.call('usbd_edpt_busy',0,2),1)
    eq('INQUIRY after reset',inquiry(v),True)
    if name=='active IN Reset and READ':
     in_complete(v,2);command(v,0x28);eq('new READ IN',v.call('usbd_edpt_busy',0,0x82),1)
    if name=='three Reset reconfigurations':
     for cycle in range(2):
      v.begin();eq('repeated reset complete',v.finish(),True);configure(v);eq('new CBW',v.call('usbd_edpt_busy',0,2),1)
   eq('no NVIC shutdown',any(w['address']==hex(0xe000e18c) and w['value']&32 for w in v.nvic),False)
   if name=='RX Flush stuck':eq('only one retry',v.rx_flushes-rx0,2)
   if name=='RX first attempt stalls then retry succeeds':eq('second explicit RX request used',v.rx_flushes-rx0,2)
   eq('OUT0 never receives EPDIS',any(w['address']==hex(BASE+0xb00) and int(w['value'],16)&(1<<30) for w in v.writes[before:]),False)
   eq('FIFO layout unchanged',any(w['address'] in [hex(BASE+0x24),hex(BASE+0x28),hex(BASE+0x104),hex(BASE+0x108)] for w in v.writes[before:]),False)
 except Exception as e:error=str(e)
 row=dict(name=name,passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,calls=v.calls,register_writes=v.writes,flushes=v.flush_log,class_callbacks=v.class_log,phase=v.phase(),failure=v.cpu.mem_read(symbols['reset_progress']+2,1)[0],nvic=v.nvic);rows.append(row)
 print(name,row['passed'],error,[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],flush=True)
report=dict(artifact=str(axf),axf_sha256=hashlib.sha256(axf.read_bytes()).hexdigest(),summary=dict(cases=len(rows),passed=sum(r['passed'] for r in rows),assertions=sum(len(r['checks']) for r in rows)),cases=rows,hardware='NOT_TESTED',model='Only explicit command responses; USBRST never clears EPENA or RXFLVL')
p=HERE/'reports/r2-reset.json'
if p.exists():(HERE/'reports'/('r2-reset-history-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(p.read_bytes())
p.write_text(json.dumps(report,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in rows))
