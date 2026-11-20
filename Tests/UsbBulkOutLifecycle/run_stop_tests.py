"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1c1/stop';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Tests/UsbBulkOutLifecycle/reports';REPORT.mkdir(parents=True,exist_ok=True)
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
for name in ['Tests/UsbBulkOutLifecycle/stop_test.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c','Tests/UsbBulkOutLifecycle/boundary_stubs.c']:
 o=OUT/(Path(name).stem+'.o');run(args+['-c',ROOT/name,'-o',o]);objects.append(o)
run([cc/'armlink.exe',*objects,'--entry','Stop_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]



from harness import VM,BASE,GINT,MASK,DCTL,DMASK,OMASK,IMASK
from unicorn.arm_const import UC_ARM_REG_IPSR
fields='result stage attempt sequence owned fenced received pio events returns lease barrier disable_requested disable_observed events_retired normal_complete lease_done error first_error reconciles owner bulk_mask iso_mask lease_fault out_calls in_calls setups isos busy nvic class_memory session transfer_session data next_data aux setup0 setup1 unexpected releasing ep1_owned ep1_fenced ep1_returns ep0_owned irq_enabled ep0_busy'.split()
names=['never-armed idle Stop','active EP2 cancel','queued 31-byte packet drained before stop','RXFLVL pending cannot retire before service','normal completion before Stop','normal XFRC wins race after fence','XFRC and EPDISD in same snapshot','normal callback rearm then cancel new identity','submission fence rejects next Receive','EP0 SETUP during target stop','other OUT completion remains normal','shared FIFO other OUT Buffer preserved','ISO claims lease before Bulk','ISO joins Bulk lease','Bulk leaves valid ISO lease held','unrelated EPDISD cannot clear lease','unknown hardware NAK not stolen','GONAKEFF timeout','disable timeout','EPENA clear without retired interrupt insufficient','Reset retains old Buffer','Disconnect retains old Buffer','repeat Stop and Commit','forged transfer rejected','ISR cannot Begin Stop','overflow veto preserves Buffer and other packet','corrupt HAL pointer veto','Reset during actual PIO copy','stalled clock bounded by polls','Attempt exhaustion fences old transfer','lease exhaustion retains Buffer','transfer sequence exhaustion no submission','same-attempt recovery from late NAK','same-attempt recovery from late Disable','old Commit cannot retire newly armed transfer','late raw XFRC cannot complete active new transfer','late direct callback cannot retire new transfer','IN progresses during OUT stop','masked RXFLVL cannot produce NAK proof','EP0 control data uses guarded Buffer','release command is not release completion','late release completion recovers original attempt','legacy Close Abort Flush Stop cannot bypass proof','flash destination rejected before HAL submit','enable cannot reset live controller','ISO inactive disable cannot fake completion','ISO cannot steal releasing lease','lost NAK barrier cannot revive','Reset between W1C and callback','EP0 zero status with Platform preparation','EP0 zero status with HAL setup receiver','late opt-in after legacy session refused','XFRC waits for actual remaining RX consumption','Stop takes over deferred normal completion','EP0 cannot replace a still-owned receive','new SETUP retains unfinished EP0 data owner','single-EP RX Flush remains denied after Commit']
rows=[]
for case,name in enumerate(names,1):
 vm=VM(symbols,sections,'Stop_Step',{'g_stop_observation':fields});checks=[];error=None
 def step(op,arg=0):return vm.step(op,arg)['g_stop_observation']
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def arm():
  v=step(1);eq('Receive admitted',v['result'],0);eq('source held',v['owned'],1);return v
 def begin(expected=0):
  v=step(2);eq('snapshot valid',v['result'],0);v=step(3);eq('Begin registration result',v['result'],expected);return v
 def queue_before_marker(ep,data):
  vm.queue(ep,data);packet=vm.q.pop();index=next((i for i,x in enumerate(vm.q) if ((x[0]>>17)&15)==1),len(vm.q));vm.q.insert(index,packet)
 def drive(limit=35):
  for j in range(limit):
   v=step(4)
   if v['stage'] in (4,6):return v
   step(6)
  raise AssertionError('proof did not converge')
 def reach(stage,limit=30):
  for j in range(limit):
   v=step(4)
   if v['stage'] in (stage,6):return v
   step(6)
  raise AssertionError('stage not reached')
 def commit():
  v=drive();eq('proof reached',v['stage'],4);eq('PIO returned',v['pio'],0);v=step(5)
  eq('Commit succeeds',v['result'],0);eq('Commit state',v['stage'],5);eq('owned returned only by Commit',v['owned'],0);eq('busy cleared',v['busy'],0);return v
 def iso_event():
  vm.wr(BASE+0x808,((vm.rd(BASE+0xb20)>>16)&1)<<8);vm.wr(MASK,vm.rd(MASK)|(1<<21));vm.wr(GINT,vm.rd(GINT)|(1<<21))
 def finish_release():
  vm.hold_release=False;vm.wr(DCTL,vm.rd(DCTL)&~8);vm.wr(GINT,vm.rd(GINT)&~(1<<7));vm.nak_requested=False
 try:
  iso=case in (13,14,15,46,47)
  v=step(0,2 if case==52 else 1 if iso else 0);eq('real BSP/Platform initialization',v['result'],0xfffffffc if case==52 else 0);vm.wr(BASE+0x40,0x4f54310a)
  if case==52:
   eq('no transfer identity fabricated',v['sequence'],0)
  elif case==1:
   begin();v=commit();eq('never armed no Buffer return',v['returns'],0);eq('no invalid EPDIS command',v['disable_requested'],0)
  elif case in (53,54,55,56):
   arm()
   if case in (53,54):
    vm.queue(2,b'FIRST');vm.queue(2,b'LAST');vm.complete(2);v=step(6)
    eq('no completion before remaining RX data consumed',v['out_calls'],0);eq('old Buffer still held',v['owned'],1)
    if case==54:begin()
    v=step(6)
    if case==53:eq('normal completion delivered after actual drain',v['out_calls'],1);eq('one source return',v['returns'],1)
    else:eq('Stop suppresses deferred normal callback',v['out_calls'],0);commit()
   else:
    v=step(29);eq('EP0 data owner admitted',v['result'],0)
    if case==55:
     v=step(29);eq('replacement rejected',v['result']!=0,True);eq('EP0 busy retained with old Buffer',v['ep0_busy'],1)
    else:
     vm.queue(0,b'SETUP123',kind=6);v=step(6)
     eq('EP0 old data still held',v['ep0_owned'],1);eq('EP0 busy retained after new Setup',v['ep0_busy'],1)
   eq('USB IRQ remains enabled',step(99)['irq_enabled'],1)
  elif case in (32,44):
   if case==32:step(26);v=step(1);eq('sequence exhaustion rejected',v['result'],0xfffffffa)
   else:v=step(24);eq('Flash destination rejected',v['result'],0xffffffff)
   eq('no buffer captured',v['owned'],0);eq('not armed in hardware',vm.rd(BASE+0xb40)&(1<<31),0)
  else:
   arm()
   if case in (3,4,11,12,21,22,26):
    if case in (11,12,26):step(12);vm.queue(1,b'AUX!')
    vm.queue(2,b'X'*(68 if case==26 else 31))
   if case in (13,14,15,46,47):step(12)
   if case in (5,8):
    vm.queue(2,b'DONE');step(6)
    if case==8:step(13)
    vm.complete(2);v=step(6);eq('normal completion forwarded',v['out_calls'],1)
    if case==5:eq('normal path returned Buffer',v['owned'],0)
    else:eq('callback rearmed a new transfer',v['sequence'],2);eq('new buffer captured',v['owned'],1)
   if case==13:iso_event();step(6)
   if case==18 or case==33:vm.auto_nak=False
   if case in (19,34):vm.hold_disable.add(2)
   if case==15:vm.hold_disable.add(1)
   if case in (41,42,47):vm.hold_release=True
   if case==17:vm.wr(GINT,vm.rd(GINT)|(1<<7));vm.wr(DCTL,vm.rd(DCTL)|8)
   if case==30:step(25)
   if case==31:step(27)
   if case==25:
    step(2);vm.cpu.reg_write(UC_ARM_REG_IPSR,16);v=step(3);vm.cpu.reg_write(UC_ARM_REG_IPSR,0)
    eq('ISR rejected',v['result'],0xfffffffc);eq('no Attempt created',v['attempt'],0);eq('source still held',v['owned'],1)
   elif case in (43,45):
    if case==45:v=step(30);eq('cannot re-enable live controller',v['result'],0xfffffffc)
    else:
     for op in (18,19,20,17,16):v=step(op);eq('legacy exit cannot bypass proof '+str(op),v['result']!=0,True);eq('Buffer retained '+str(op),v['owned'],1)
   else:
    v=begin(0xfffffffa if case==30 else 0);attempt=v['attempt'];identity=v['sequence'];eq('Attempt allocation valid',attempt==0 if case==30 else attempt!=0,True)
    if case in (17,31):
     eq('failed claim enters recovery',v['stage'],6);eq('failed claim keeps Buffer',v['owned'],1)
    elif case==30:
     # Begin admission failed before identity allocation; proof is fenced RECOVERY_REQUIRED.
     eq('no wrapped Attempt',v['attempt'],0);eq('fault fenced',v['fenced'],1);eq('source retained',v['owned'],1)
    elif case==4:
     v=step(4);eq('no RX consumer no proof',v['stage'],1);eq('Buffer not released',v['owned'],1);eq('no return counter',v['returns'],0)
     v=commit();eq('queued bytes received by real HAL',v['received'],31)
    elif case==6:
     vm.complete(2);v=step(6);eq('fenced callback not forwarded',v['out_calls'],0);v=commit();eq('one final return',v['returns'],1)
    elif case==7:
     reach(2);vm.event(2,3);v=step(6);eq('both facts retained',v['events']&3,3);eq('no ordinary callback',v['out_calls'],0);commit()
    elif case==9:
     v=step(8);eq('fence blocks new receive',v['result'],0xfffffffb);eq('identity stable',v['sequence'],identity);commit()
    elif case==10:
     vm.queue(0,b'SETUP123',6)
     for j in range(30):
      v=step(4);step(6)
      if v['stage']==6:break
      if step(99)['setup0']==0x55544553:vm.event(0,8)
      if step(99)['stage']==4:break
     v=commit();step(6);v=step(99);eq('EP0 setup bytes protected',v['setup0'],0x55544553);eq('Setup dispatched',v['setups']>0,True)
    elif case in (11,12):
     step(6)
     if case==11:vm.complete(1)
     v=commit();eq('other receiver data preserved',v['aux'],0x21585541);eq('target data received',v['received'],31)
     eq('other ownership independent',v['ep1_owned'],0 if case==11 else 1)
    elif case in (13,14,15):
     if case in (14,15):iso_event();step(6)
     v=commit();eq('target returned once',v['returns'],1)
     if case==15:
      eq('valid ISO owner still holds lease',v['owner']!=0,True);eq('ISO participant remains',v['iso_mask'],2);eq('ISO buffer retained',v['ep1_owned'],1)
      vm.hold_disable.clear();step(6);v=step(99);eq('ISO eventually retires once',v['ep1_returns'],1)
     else:eq('ISO callback after real disable',v['isos'],1);eq('ISO returned exactly once',v['ep1_returns'],1)
     eq('one shared NAK request',sum(int(x['address'],16)==DCTL and bool(int(x['value'],16)&512) for x in vm.writes),1)
    elif case==16:
     step(6);vm.event(1,2);v=step(6);eq('unrelated EPDISD did not release lease',v['owner']!=0,True);eq('NAK remains effective',vm.rd(GINT)&128,128)
    elif case in (18,19,20,33,34):
     if case in (19,20,34):reach(2)
     if case==20:
      # Hardware stops, but no HAL event hook runs.
      vm.wr(BASE+0xb40,vm.rd(BASE+0xb40)&~((1<<31)|(1<<30)));vm.wr(BASE+0xb48,0);v=step(4);eq('EPENA zero alone insufficient',v['disable_observed'],0)
     step(7,21);v=step(4);eq('bounded timeout',v['stage'],6);eq('original timeout recorded',v['first_error'],0xfffffff8);eq('Buffer retained',v['owned'],1)
     if case in (33,34):
      if case==33:vm.queue(0,kind=1)
      else:vm.hold_disable.clear()
      step(6);v=step(11);eq('same Attempt recovery admitted',v['result'],0);eq('same identity retained',v['attempt'],attempt);v=commit();eq('first timeout preserved after recovery',v['first_error'],0xfffffff8)
    elif case in (21,22):
     vm.wr(GINT,vm.rd(GINT)|(1<<12 if case==21 else 1<<2))
     if case==22:vm.wr(BASE+4,1<<2)
     v=step(6);eq('boundary locks recovery',v['stage'],6);eq('old Buffer held',v['owned'],1);eq('old identity invalidated',v['session']!=v['transfer_session'],True);eq('old data not copied',v['data'],0xaaaaaaaa)
    elif case==23:
     v=step(3);eq('same Begin Attempt',v['attempt'],attempt);v=commit();v=step(5);eq('repeat Commit idempotent',v['result'],0);eq('one return only',v['returns'],1)
    elif case==24:
     v=step(10);eq('forged ID rejected',v['result'],0xfffffffc);eq('real source unaffected',v['owned'],1);commit()
    elif case==26:
     step(6);v=step(6);eq('overflow fault retained',v['stage'],6);eq('original buffer untouched',v['data'],0xaaaaaaaa);eq('other data not flushed',v['aux'],0x21585541);eq('Buffer retained after invalid copy',v['owned'],1)
    elif case==27:
     step(23);queue_before_marker(2,b'BAD!');v=step(6);eq('pointer mismatch denied',v['stage'],6);eq('buffer unchanged',v['data'],0xaaaaaaaa);eq('buffer retained',v['owned'],1)
    elif case==28:
     queue_before_marker(2,b'COPY');vm.inject_reset_on_data=True;v=step(6);eq('reference returned after copy completes',v['pio'],0);eq('old Buffer still owned on Reset',v['owned'],1);eq('session fault retained',v['stage'],6)
    elif case==29:
     step(28);v=step(4);eq('frozen clock bounded',v['stage'],6);eq('timeout diagnostic',v['first_error'],0xfffffff8)
    elif case in (35,36,37):
     commit();v=step(8);eq('new receive admitted',v['result'],0);eq('new identity allocated',v['sequence']!=identity,True)
     if case==35:v=step(5);eq('old Commit rejected',v['result'],0xfffffffc)
     if case==36:vm.event(2,1);v=step(6);eq('late flag not new completion',v['out_calls'],0)
     if case==37:v=step(21);eq('late direct callback ignored',v['out_calls'],0)
     eq('new Buffer retained',v['owned'],1)
    elif case==38:
     v=step(14);eq('IN submitted during OUT stop',v['result'],0);vm.wr(BASE+0x920,vm.rd(BASE+0x920)&~(1<<31));vm.wr(BASE+0x928,1);v=step(6);eq('IN completes independently',v['in_calls'],1);commit()
    elif case==39:
     vm.wr(MASK,vm.rd(MASK)&~(1<<4));step(6);v=step(4);eq('no consumer no barrier',v['barrier'],0);eq('old Buffer not returned',v['owned'],1)
    elif case in (40,50,51):
     if case==40:
      v=step(29);eq('control OUT prepared',v['result'],0);vm.q.insert(0,(0|(4<<4)|(2<<17),[0x01020304]))
     elif case==50:step(32);vm.q.insert(0,(0|(2<<17),[]))
     else:step(33);vm.q.insert(0,(0|(2<<17),[]))
     step(6);vm.complete(0);v=step(6);eq('EP0 normal completion forwarded',v['out_calls']>0,True);commit()
    elif case in (41,42,47):
     reach(3);v=step(4);eq('release pending not proof',v['stage'],3);eq('release retains Buffer',v['owned'],1);eq('lease remains owned',v['owner']!=0,True);eq('release command submitted',v['releasing'],1)
     if case==42:
      step(7,21);v=step(4);eq('release timeout fault',v['stage'],6);finish_release();v=step(11);eq('late clear reconciled same Attempt',v['result'],0);v=commit();eq('original failure kept',v['first_error'],0xfffffff8)
     elif case==47:
      iso_event();v=step(6);eq('ISO cannot take releasing lease',v['ep1_owned'],1);eq('no ISO return on rejected claim',v['ep1_returns'],0)
     else:v=step(5);eq('premature Commit fails',v['result']!=0,True);eq('no premature return',v['returns'],0)
    elif case==46:
     # ISO cached active state has gone idle without its disable event.
     step(6);vm.wr(BASE+0xb20,vm.rd(BASE+0xb20)&~(1<<31));iso_event();v=step(6);eq('no ISO buffer release from EPENA alone',v['ep1_owned'],1)
    elif case==48:
     step(6);vm.wr(GINT,vm.rd(GINT)&~128);vm.wr(DCTL,vm.rd(DCTL)&~8);v=step(4);eq('lost barrier is permanent fault',v['stage'],6);eq('source retained',v['owned'],1)
    elif case==49:
     vm.complete(2);vm.inject_reset_on_terminal=True;v=step(6);eq('Reset before callback blocks old completion',v['out_calls'],0);eq('old buffer retained',v['owned'],1);eq('fault state',v['stage'],6)
    elif case==57:
     commit();step(33);v=step(20);eq('single-EP shared Flush rejected with HAL Setup receiver',v['result']!=0,True);eq('no RX flush issued',vm.rx_flushes,0)
    else:
     nvic=v['nvic'];v=commit();eq('Buffer return exactly once',v['returns'],1)
     if case==2:eq('Stop phases do not toggle USB NVIC',v['nvic'],nvic)
   v=step(99);eq('USB IRQ enabled on return',v['irq_enabled'],1);eq('Class memory not released',v['class_memory'],1);eq('no shared RX flush',vm.rx_flushes,0)
  eq('no unintended Core/board boundary',step(99)['unexpected'],0);eq('caller PRIMASK restored',vm.trace[-1]['primask'],0)
 except Exception as e:error=str(e)
 passed=error is None and all(c['passed'] for c in checks);rows.append(dict(id=f'C1-{case:02}',name=name,passed=passed,error=error,assertions=checks,trace=vm.trace,reads=vm.reads,writes=vm.writes));print(rows[-1]['id'],'PASS' if passed else 'FAIL',error,[x for x in checks if not x['passed']],flush=True)
report=dict(baseline='3305d4a729e965247b33c95e5558eac576f1c6ef',cases=rows,commands=commands,summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['CM7/BSP/bsp_usb.c','CM7/BSP/bsp_usb.h','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_pcd.h','Tests/UsbBulkOutLifecycle/stop_test.c','Tests/UsbBulkOutLifecycle/harness.py','Tests/UsbBulkOutLifecycle/run_stop_tests.py']},model='hardware register transitions only; ownership/refcount/proof updated solely by real BSP/HAL code; no silicon timing proof',hardware='NOT_TESTED')
prior=REPORT/'stop.json'
if prior.exists():(REPORT/('stop-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
prior.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert all(x['passed'] for x in rows)
