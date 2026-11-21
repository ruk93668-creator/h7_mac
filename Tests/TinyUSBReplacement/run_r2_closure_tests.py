"""Actual full Keil AXF closure tests. Hardware transitions only; never edit stop proof/busy.
Reuse the R2 real-FreeRTOS image loader/MMIO definitions, not its historical assertions.
"""
from pathlib import Path
import json,hashlib,struct
HERE=Path(__file__).resolve().parent
exec((HERE/'run_r2_firmware_tests.py').read_text(encoding='utf-8').split('rows=[]')[0],globals())
from unicorn import UC_HOOK_MEM_WRITE,UC_HOOK_CODE
from elftools.elf.elffile import ELFFile
with axf.open('rb') as f:sizes={s.name:s['st_size'] for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols()}

def setup_board(v):
 assert v.init()==1
 v.reset();v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0)
def task(v):v.irq();v.call('tud_task_ext',0,0)
def drive(v,limit=48):
 for i in range(limit):
  task(v)
  if v.call('usbd_device_failed'):return False
  if not v.call('dcd_device_pending',0) and not v.cpu.mem_read(symbols['config_close_pending'],1)[0] and not v.cpu.mem_read(symbols['unplug_close_pending'],1)[0]:return True
 return False
def command(v,op):
 cdb=bytearray(16);cdb[0]=op;cdb[8]=1
 out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,103,512,0x80 if op==0x28 else 0,0,10,bytes(cdb)))
 if op==0x28:v.call('TinyUSB_RamPoll');v.call('tud_task_ext',0,0)
def inquiry(v):
 start=len(v.writes);cdb=bytes([0x12,0,0,0,36,0])+b'\0'*10
 out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,104,36,0x80,0,6,cdb));in_complete(v,2)
 data=bulk_bytes(v,start);return len(data)>=52 and data[-16:-8]==struct.pack('<II',0x53425355,104) and data[-4]==0

def cortex_hook(v):
 v.nvic=[]
 def write(c,access,a,size,value,ctx):v.nvic.append(dict(address=hex(a),value=value))
 v.cpu.hook_add(UC_HOOK_MEM_WRITE,write,begin=0xe000e100,end=0xe000e1ff)
def irq_shutdowns(v):return [x for x in v.nvic if x['address']==hex(0xe000e18c) and x['value']&32]
def source(v,ep):return v.rd(symbols['xfer_status']+(ep&15)*32+(16 if ep&128 else 0))
def class_bytes(v):return bytes(v.cpu.mem_read(symbols['_mscd_itf'],sizes['_mscd_itf']))
rows=[]
names=['OUT config zero then configure','IN config zero then configure','superseding descriptor keeps EP0 and no old status','new configuration supersedes old pending configuration','OUT Disconnect then legal reconnect','IN Disconnect then legal reconnect','active IN DeInit then Init and enumerate','active OUT DeInit then Init and enumerate','DeInit failure retains class and IRQ','DeInit pending keeps IRQ and pullup','Backend pending across unconfigure and configure','Backend pending refuses DeInit before any shutdown','missing Global NAK bounded failure retains source','missing Disable bounded failure retains source','old queued completion cannot affect new CBW','normal IN completion races group closure','Reset during group closure keeps controlled fault','active Bulk Bus Reset remains hardware BLOCKED','soft Disconnect then reconnect','ten direct DeInit Restart cycles','group clock stops during RX drain','two OUT endpoints share one NAK lease','foreign Global NAK owner fails without clearing it','EP0 active DeInit keeps IRQ and control source','Connect after DeInit refuses invalid rhport']
for name in names:
 v=Model();checks=[];error=None
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  setup_board(v);cortex_hook(v)
  if name.startswith('Connect after'):
   eq('DeInit pending',v.call('tud_deinit',0),0);eq('drain',drive(v),True);eq('DeInit success',v.call('tud_deinit',0),1);eq('Connect without Init refused',v.call('tud_connect'),0)
  elif name.startswith('soft Disconnect'):
   command(v,0x28);eq('soft disconnect initially pending',v.call('tud_disconnect'),0);eq('connect refused during pending',v.call('tud_connect'),0)
   eq('native Task retires',drive(v),True);eq('pullup removed after proof',v.rd(DCTL)&2,2);eq('idempotent soft disconnect',v.call('tud_disconnect'),1);eq('legal connect',v.call('tud_connect'),1)
   v.reset();v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0);eq('new CBW',v.call('usbd_edpt_busy',0,2),1);eq('new INQUIRY',inquiry(v),True)
  elif name.startswith('ten direct'):
   previous=v.call('dcd_edpt_generation',0,2)
   for cycle in range(10):
    eq('DeInit begins pending',v.call('tud_deinit',0),0);eq('retirement',drive(v),True);eq('DeInit after proof',v.call('tud_deinit',0),1);eq('Init',v.init(),1)
    v.reset();v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0);now=v.call('dcd_edpt_generation',0,2);eq('new generation',now>previous,True);previous=now;eq('new CBW busy',v.call('usbd_edpt_busy',0,2),1)
  elif name.startswith('group clock'):
   # The old CBW source and pending Global NAK RX marker remain owned.
   # Group progress needs a real clock, not an emulator instruction budget.
   eq('group starts',v.call('dcd_edpt_close_all_safe',0),3)
   v.stall_clock=True;eq('stopped clock FAILED',v.call('dcd_edpt_close_all_safe',0),2);eq('fault retained',v.call('usbd_device_failed'),1)
  elif name.startswith('two OUT'):
   eq('peer open',v.open(1),1);v.cpu.mem_write(0x2001e800,b'Z'*64);eq('peer receive',v.call('usbd_edpt_xfer',0,1,0x2001e800,64),1)
   v.setup(bytes([0,9,0,0,0,0,0,0]));v.queue(1,b'Q'*64);v.queue(1,kind=3);v.complete(1)
   eq('both endpoints retire',drive(v),True);eq('peer old buffer not copied after fence',bytes(v.cpu.mem_read(0x2001e800,64))==b'Z'*64,True);eq('peer source returned',source(v,1),0);eq('MSC source returned',source(v,2),0);eq('NAK released',v.rd(DCTL)&8,0)
  elif name.startswith('foreign'):
   v.wr(DCTL,v.rd(DCTL)|8);v.wr(GINT,v.rd(GINT)|(1<<7));old=source(v,2);before=v.call('dcd_edpt_generation',0,0x80);v.setup(bytes([0,9,0,0,0,0,0,0]))
   eq('foreign owner FAILED',v.call('usbd_device_failed'),1);eq('owner not cleared',v.rd(DCTL)&8,8);eq('source retained',source(v,2),old);eq('no Status ZLP',v.call('dcd_edpt_generation',0,0x80),before)
  elif name.startswith('EP0 active'):
   v.setup(bytes([0x80,6,0,1,0,0,18,0]));old=source(v,0x80);eq('DeInit pending on Bulk',v.call('tud_deinit',0),0);eq('Bulk retirees complete',drive(v),True)
   eq('EP0 still denies DeInit',v.call('tud_deinit',0),0);eq('EP0 source retained',source(v,0x80),old);eq('IRQ kept',len(irq_shutdowns(v)),0);eq('no software fault for pending control completion',v.call('usbd_device_failed'),0)
   in_complete(v,0);out_packet(v,0,b'');eq('DeInit after real EP0 completion',v.call('tud_deinit',0),1)
  elif name.startswith(('OUT config','IN config')):
   command(v,0x28 if name.startswith('IN') else 0x2a);address=0x82 if name.startswith('IN') else 2
   before=v.call('dcd_edpt_generation',0,0x80);v.setup(bytes([0,9,0,0,0,0,0,0]))
   eq('no early Status',v.call('dcd_edpt_generation',0,0x80),before);eq('old source held',source(v,address)!=0,True)
   eq('closure completes',drive(v),True);eq('one final Status',v.call('dcd_edpt_generation',0,0x80),before+1);eq('old source retired',source(v,address),0)
   in_complete(v,0);v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0);eq('new CBW',v.call('usbd_edpt_busy',0,2),1);eq('new INQUIRY/CSW',inquiry(v),True)
  elif name.startswith('superseding'):
   v.setup(bytes([0,9,0,0,0,0,0,0]));v.setup(bytes([0x80,6,0,1,0,0,18,0]));new_gen=v.call('dcd_edpt_generation',0,0x80)
   eq('old group retires',drive(v),True);eq('old Status not added',v.call('dcd_edpt_generation',0,0x80),new_gen);eq('new descriptor 18 bytes',v.rd(BASE+0x910)&0x7ffff,18)
   in_complete(v,0);eq('new descriptor completion preserved',v.call('usbd_edpt_busy',0,0),1);out_packet(v,0,b'');eq('no fault',v.call('usbd_device_failed'),0)
  elif name.startswith('new configuration'):
   v.setup(bytes([0,9,0,0,0,0,0,0]));before=v.call('dcd_edpt_generation',0,0x80);v.setup(bytes([0,9,1,0,0,0,0,0]));eq('no early status',v.call('dcd_edpt_generation',0,0x80),before)
   eq('same closure finishes',drive(v),True);eq('only latest status',v.call('dcd_edpt_generation',0,0x80),before+1);eq('latest config CBW',v.call('usbd_edpt_busy',0,2),1)
  elif 'Disconnect' in name:
   command(v,0x28 if name.startswith('IN') else 0x2a);a=0x82 if name.startswith('IN') else 2;old=class_bytes(v)
   v.wr(BASE+4,4);v.wr(GINT,v.rd(GINT)|4);v.irq()
   eq('IRQ does not reset class',class_bytes(v)==old,True);eq('buffer before Task retained',source(v,a)!=0,True)
   eq('Task retirement completes',drive(v),True);eq('old source gone only after proof',source(v,a),0);eq('no shutdown of IRQ',len(irq_shutdowns(v)),0)
   v.reset();v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0);eq('reconnected CBW',v.call('usbd_edpt_busy',0,2),1);eq('reconnected INQUIRY',inquiry(v),True)
  elif name.startswith(('active IN DeInit','active OUT DeInit')):
   command(v,0x28 if 'IN ' in name else 0x2a);eq('first DeInit pending',v.call('tud_deinit',0),0);eq('no early IRQ shutdown',len(irq_shutdowns(v)),0)
   eq('group progresses in existing Task',drive(v),True);eq('actual DeInit succeeds',v.call('tud_deinit',0),1);eq('Init',v.init(),1);v.reset();v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0);eq('restart CBW',v.call('usbd_edpt_busy',0,2),1);eq('restart INQUIRY',inquiry(v),True)
  elif name.startswith('DeInit failure'):
   command(v,0x28);old=class_bytes(v);eq('first DeInit pending',v.call('tud_deinit',0),0);eq('group stops',drive(v),True)
   v.nvic.clear();v.hold_reset=True;prior=v.rd(BASE+8)&1;eq('stuck CSRST fails',v.call('tud_deinit',0),0)
   eq('class retained',class_bytes(v)==old,True);eq('NVIC never disabled on failure',len(irq_shutdowns(v)),0);eq('GINT admission restored',v.rd(BASE+8)&1,prior);eq('no pullup shutdown',v.rd(DCTL)&2,0);eq('fault prevents Init',v.init(),0)
  elif name.startswith('DeInit pending'):
   eq('pending request',v.call('tud_deinit',0),0);eq('still pending',v.call('tud_deinit',0),0);eq('no IRQ disable',len(irq_shutdowns(v)),0);eq('pullup stays',v.rd(DCTL)&2,0);eq('progress completes',drive(v),True)
  elif name.startswith('Backend pending across'):
   cdb=bytes([0x28,0,0,0,0,0,0,0,1,0])+b'\0'*6;out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,106,512,0x80,0,10,cdb));eq('backend owns buffer',v.call('TinyUSB_RamDrained'),0)
   v.setup(bytes([0,9,0,0,0,0,0,0]));eq('group closes',drive(v),True);in_complete(v,0);v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0)
   eq('new CBW blocked by old IO',v.call('usbd_edpt_busy',0,2),0);eq('old IO ownership retained',v.call('TinyUSB_RamDrained'),0);v.call('TinyUSB_RamPoll');v.call('tud_task_ext',0,0);eq('CBW only after real IO completion',v.call('usbd_edpt_busy',0,2),1)
  elif name.startswith('Backend pending refuses'):
   cdb=bytes([0x28,0,0,0,0,0,0,0,1,0])+b'\0'*6;out_packet(v,2,struct.pack('<IIIBBB16s',0x43425355,107,512,0x80,0,10,cdb));old=class_bytes(v)
   eq('DeInit refused',v.call('tud_deinit',0),0);eq('backend retained',v.call('TinyUSB_RamDrained'),0);eq('class retained',class_bytes(v)==old,True);eq('no IRQ shutdown',len(irq_shutdowns(v)),0)
  elif name.startswith('missing'):
   if 'Global' in name:v.auto_nak=False
   else:v.hold_disable.add(2)
   old=source(v,2);before=v.call('dcd_edpt_generation',0,0x80);v.setup(bytes([0,9,0,0,0,0,0,0]))
   for i in range(8):task(v)
   v.wr(symbols['xTickCount'],51);task(v)
   eq('bounded fault',v.call('usbd_device_failed'),1);eq('old source',source(v,2),old);eq('old busy',v.call('usbd_edpt_busy',0,2),1);eq('no Status',v.call('dcd_edpt_generation',0,0x80),before);eq('no IRQ shutdown',len(irq_shutdowns(v)),0)
  elif name.startswith('old queued'):
   oldgen=v.call('dcd_edpt_generation',0,2);v.setup(bytes([0,9,0,0,0,0,0,0]));eq('closure',drive(v),True);in_complete(v,0);v.setup(bytes([0,9,1,0,0,0,0,0]));in_complete(v,0)
   event=bytearray(20);event[1]=8;event[8]=2;struct.pack_into('<I',event,12,31);struct.pack_into('<I',event,16,oldgen);v.cpu.mem_write(0x2001d200,bytes(event));v.call('dcd_event_handler',0x2001d200,0);v.call('tud_task_ext',0,0)
   eq('old completion cannot release new CBW',v.call('usbd_edpt_busy',0,2),1);eq('new generation distinct',v.call('dcd_edpt_generation',0,2)>oldgen,True)
  elif name.startswith('normal IN'):
   command(v,0x28);in_complete(v,2);v.setup(bytes([0,9,0,0,0,0,0,0]));eq('normal completion/stop race retires',drive(v),True);eq('no fault',v.call('usbd_device_failed'),0)
  elif name.startswith('Reset during'):
   v.setup(bytes([0,9,0,0,0,0,0,0]));v.wr(GINT,v.rd(GINT)|(1<<12));v.irq();eq('unproven overlap preserved fault',v.call('usbd_device_failed'),1);eq('no Init override',v.init(),0)
  else:
   command(v,0x28);old=class_bytes(v);v.wr(GINT,v.rd(GINT)|(1<<12));v.irq();eq('active reset retains fault',v.call('usbd_device_failed'),1);eq('class not reset without proof',class_bytes(v)==old,True)
 except Exception as e:error=str(e)
 row=dict(name=name,passed=not error and all(c['passed'] for c in checks),checks=checks,error=error,calls=v.calls,register_writes=v.writes,nvic=getattr(v,'nvic',[]));rows.append(row);print(name,row['passed'],error,[(c['name'],c['actual'],c['expected']) for c in checks if not c['passed']],flush=True)
report=dict(baseline='daa6d7c82412c883555fd8c6d3fd9a9f80f0c4a3',artifact=str(axf),axf_sha256=hashlib.sha256(axf.read_bytes()).hexdigest(),summary=dict(cases=len(rows),passed=sum(r['passed'] for r in rows),assertions=sum(len(r['checks']) for r in rows)),cases=rows,active_reset_acceptance='BLOCKED: manual-specific all-OUT reset sequence and EP0 exception not proven',hardware='NOT_TESTED')
p=HERE/'reports/r2-closure.json'
if p.exists():(HERE/'reports'/('r2-closure-history-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(p.read_bytes())
p.write_text(json.dumps(report,indent=2),encoding='utf-8');raise SystemExit(not all(r['passed'] for r in rows))
