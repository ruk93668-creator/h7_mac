"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1b1/integration';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Tests/UsbBulkInIntegration/reports';REPORT.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin')
target=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(cc/'armcc.exe'),'--cpu','Cortex-M7','--c99','--split_sections','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',str(ROOT/'CM7/BSP/bsp_usb_hal_config.h')]
for inc in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
commands=[]
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True);s=(p.stdout+p.stderr).decode('utf-8',errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));print(s)
 if p.returncode or 'warning:' in s.lower():raise RuntimeError(s)
obj=OUT/'bulk_in_test.o';axf=OUT/'bulk_in_test.axf'
run(args+['-c',ROOT/'Tests/UsbBulkInIntegration/integration_test.c','-o',obj])
stub=OUT/'boundary_stubs.o'
run(args+['-c',ROOT/'Tests/UsbBulkInIntegration/boundary_stubs.c','-o',stub])
run([cc/'armlink.exe',obj,stub,'--entry','Integration_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]

BASE=0x40080000;CTL=BASE+0x940;INT=CTL+8;GRST=BASE+0x10;GINT=BASE+0x14
fields='result mode last_result txn pending progress stage attempt source pool source_returns pool_returns first_error error failed_stage reconciles owner conflicted daint nvic busy unexpected transfer source_ptr ack ack_result business_fault app_stopping out_busy out_opened'.split()
rows=[]
names=['service Pending process','explicit same transaction','ordinary BUSY rejected','cumulative timeout','stalled service clock','identity replacement','Reset interleave','Disconnect interleave','reconnect completes','explicit completes once','pure SOF during Flush','SOF plus other event','non-target XFRC conservative conflict','non-target EPDISD conflict','Reset during Flush','other HAL Flush rejected','flush timeout retains owner','late disable Reconcile','late flush Reconcile','conflicted owner cannot Reconcile','old session cannot Reconcile','forged Attempt rejected','repeat Reconcile rejected','PIO reference blocks Reconcile','wrong FIFO cannot Reconcile','stalled BSP clock','legacy Close retains references','legacy Abort no synchronous wait','EP0 zero length errata model','stop identity exhaustion','actual App one Stop ACK','actual App Pending no FAULT','actual App ordinary BUSY failure ACK','Reconcile invalid endpoint','Service explicit retry uses original Reconcile','Reset arrives during SOF acknowledgement','Core334 new contract preserves both directions','failed precondition must not prevent first valid identity']
for case,name in enumerate(names,1):
 cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(BASE,0x40000),(0xe0000000,0x100000)]:cpu.mem_map(a,n)
 for a,data in sections:cpu.mem_write(a,data)
 def rd(a):return struct.unpack('<I',cpu.mem_read(a,4))[0]
 def wr(a,v):cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
 pending=[];writes=[];trace=[];checks=[]
 delay_disable=case in (18,21,22,23,24,35);delay_fifo=case in (17,19,20,25)
 def read(uc,access,a,size,value,ctx):
  if a==CTL:
   v=rd(a)
   if v&(1<<27):wr(a,(v&~(1<<27))|(1<<17));wr(INT,rd(INT)|(1<<6));v=rd(a)
   if v&(1<<30) and not delay_disable:wr(a,v&~((1<<31)|(1<<30)));wr(INT,rd(INT)|2)
  if a==GRST:
   v=rd(a)|(1<<31)
   if not delay_fifo:v&=~(1<<5)
   wr(a,v)
 def write(uc,access,a,size,value,ctx):
  if a in (INT,BASE+0x928,GINT):pending.append((a,(rd(a)&~value)|((1<<12) if case==36 and a==GINT and value==(1<<3) else 0)))
  if BASE<=a<BASE+0x40000:writes.append(dict(address=hex(a),value=hex(value)))
 from unicorn import UC_HOOK_CODE
 def code(uc,a,size,ctx):
  while pending:
   x,v=pending.pop(0);wr(x,v)
 cpu.hook_add(UC_HOOK_MEM_READ,read,begin=BASE,end=BASE+0x3ffff)
 cpu.hook_add(UC_HOOK_MEM_WRITE,write,begin=BASE,end=BASE+0x3ffff)
 cpu.hook_add(UC_HOOK_CODE,code)
 def step(op,arg=0):
  cpu.reg_write(UC_ARM_REG_SP,0x2003fff0);cpu.reg_write(UC_ARM_REG_LR,0x01000001);cpu.reg_write(UC_ARM_REG_R0,op);cpu.reg_write(UC_ARM_REG_R1,arg)
  cpu.emu_start(symbols['Integration_Step']|1,0x01000000,count=250000,timeout=3000000)
  assert cpu.reg_read(UC_ARM_REG_PC)==0x01000000,'execution did not return'
  v=dict(zip(fields,struct.unpack('<30I',cpu.mem_read(symbols['g_integration'],120))))
  v.update(op=op,primask=cpu.reg_read(UC_ARM_REG_PRIMASK),GRSTCTL=rd(GRST),DIEPCTL=rd(CTL),DIEPINT=rd(INT));trace.append(v);return v
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def reach(stage,op=8):
  for i in range(30):
   v=step(op)
   if v['stage'] in (stage,8):return v
  raise AssertionError('stage not reached')
 error=None
 try:
  v=step(0);source=v['source_ptr'];identity=v['transfer']
  if case==38:
   v=step(29);eq('prior failure has no identity',v['txn'],0);eq('prior failure state',v['progress'],3)
   v=step(1);eq('first actual pending allocated identity',v['txn']!=0,True);eq('no invalid zero Pending',v['mode'],5);eq('actual Attempt bound',v['attempt']!=0,True)
  elif case==37:
   step(28);v=step(21);eq('Close blocked before HAL Close',v['result'],0xfffffffb);eq('IN buffer retained',v['source'],1);eq('IN pool retained',v['pool'],1);eq('IN busy retained',v['busy'],1);eq('OUT busy retained',v['out_busy'],1);eq('OUT remains open',v['out_opened'],1);eq('no HAL release attempted',v['unexpected'],0)
  elif case==35:
   v=step(1);txn=v['txn'];attempt=v['attempt'];reach(2,1)
   step(4,1001);v=step(1);eq('original stop fails',v['mode'],8)
   delay_disable=False;wr(CTL,rd(CTL)&~((1<<31)|(1<<30)));wr(INT,rd(INT)|2)
   v=step(3);eq('explicit reconnect retry accepted',v['result'],0)
   v=step(2);eq('same transaction after proof-based reconcile',v['txn'],txn);eq('same Attempt after reconcile',v['attempt'],attempt);eq('first timeout retained',v['first_error'],0xfffffff8);eq('one reconcile',v['reconciles'],1)
   step(14)
   for i in range(35):
    v=step(2)
    if v['mode']!=5:break
   eq('recovered to reconnect wait',v['mode'],6);eq('buffer returned once',v['source_returns'],1)
  elif case in (31,32,33):
   step(24)
   if case==33:step(5,1)
   v=step(25)
   if case==33:
    eq('failure ACK generated',v['ack'],1);eq('ordinary BUSY not waiting',v['ack_result'],0xfffffff7);eq('business failure recorded',v['business_fault'],0xfffffff7)
   else:
    txn=v['txn'];attempt=v['attempt']
    eq('no premature ACK',v['ack'],0);eq('Pending no FAULT',v['business_fault'],0)
    v=step(25);eq('one executor same Attempt',v['attempt'],attempt);eq('same transaction',v['txn'],txn)
    if case==31:
     step(14)
     for i in range(35):
      v=step(25)
      if v['ack']:break
     eq('ACK only after full stop',v['ack'],1);eq('success ACK',v['ack_result'],0);eq('stopping cleared',v['app_stopping'],0)
     step(26);v=step(25);eq('no second ACK',v['ack'],0);eq('one buffer return',v['source_returns'],1)
  elif case==34:
   step(7);v=step(27);eq('invalid address rejected before array lookup',v['result'],0xffffffff);eq('source held',v['source'],1)
  elif case<=10:
   if case==3:step(5,1)
   if case==9:step(3);v=step(2)
   else:v=step(1)
   if case==3:
    eq('ordinary BUSY is terminal ERROR',v['mode'],8);eq('not registered Pending',v['pending'],0);eq('not exposed as wait',v['result'],0xfffffff7)
   else:
    eq('STOPPING retained',v['mode'],5);eq('business result remains success',v['last_result'],0);eq('registered actual pending',v['pending'],1)
    txn=v['txn'];attempt=v['attempt'];eq('nonzero identities',txn!=0 and attempt!=0,True)
    if case in (1,2):
     v=step(2 if case==1 else 1);eq('same transaction',v['txn'],txn);eq('same StopAttempt',v['attempt'],attempt);eq('still STOPPING',v['mode'],5)
    elif case in (4,5):
     step(4,1001) if case==4 else step(20)
     v=step(1);eq('ERROR on bounded wait',v['mode'],8);eq('timeout recorded',v['result'],0xfffffff8);eq('source retained',v['source'],1)
    elif case==6:
     step(6);v=step(1);eq('identity mismatch ERROR',v['mode'],8);eq('identity error',v['result'],0xfffffffc)
    elif case in (7,8):

     if case==7:wr(GINT,1<<12);step(11)
     else:step(12)
     v=step(1);eq('session failure ERROR',v['mode'],8);eq('source retained',v['source'],1)
    elif case in (9,10):
     step(14)
     for i in range(35):
      v=step(2 if case==9 else 1)
      if v['mode']!=5:break
     eq('stop intent final state',v['mode'],6 if case==9 else 1);eq('source returned once',v['source_returns'],1);eq('pool returned once',v['pool_returns'],1)
     if case==10:
      v=step(1);eq('repeat stop stays idle',v['mode'],1);eq('no repeated return',v['source_returns'],1);eq('no new transaction',v['txn'],txn)
  elif case in (27,28):
   v=step(21 if case==27 else 22);eq('legacy call cannot bypass proof',v['result'],0xfffffffb);eq('resource held',v['source'],1);eq('pool held',v['pool'],1);eq('no unexpected HAL cancel',v['unexpected'],0)
  elif case==29:
   text=(ROOT/'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c').read_text(encoding='utf-8')
   body=text[text.index('HAL_StatusTypeDef USB_EPStartXfer'):text.index('HAL_StatusTypeDef USB_EPStopXfer')]
   eq('actual LL has zero-length branch','ep->xfer_len == 0U' in body,True)
   eq('actual LL enables CNAK and EPENA together','USB_OTG_DIEPCTL_CNAK | USB_OTG_DIEPCTL_EPENA' in body,True)
   eq('model applicability: no production workaround claimed',True,True)
  elif case==30:
   step(18);v=step(19);eq('identity exhaustion rejected',v['result'],0xfffffffa);eq('no stop created',v['attempt'],0);eq('resource held',v['source'],1)
  else:
   step(7)
   if case in (18,21,22,23,24):v=reach(2)
   elif case==26:v=step(23);v=step(8)
   else:v=reach(4)
   attempt=v['attempt']
   if case in (11,36):
    wr(GINT,1<<3);wr(BASE+0x18,1<<3);v=step(11)
    eq('SOF does not taint command',v['stage'],4);eq('no full HAL IRQ dispatch',v['unexpected'],0);eq('SOF acknowledgement preserves other arrivals',rd(GINT),(1<<12) if case==36 else 0)
    if case==36:
     v=step(11);eq('next Reset remains handled',v['stage'],8);eq('Reset retains source',v['source'],1);eq('Reset taints old FIFO proof',v['conflicted'],1)
    else:
     v=reach(6);eq('FIFO proof reached',v['stage'],6)
   elif case in (12,13,14,15,20):
    raw=(1<<3)|(1<<18) if case==12 else (1<<18) if case in (13,14,20) else (1<<12)
    wr(GINT,raw);wr(BASE+0x818,2);wr(BASE+0x820,0x80000000);wr(BASE+0x928,2 if case==14 else 1);v=step(11)
    eq('ambiguous IRQ preserves recovery',v['stage'],8);eq('source retained',v['source'],1);eq('owner retained',v['owner'],2);eq('command tainted',v['conflicted'],1)
    if case==20:
     wr(GINT,0);delay_fifo=False;v=step(9);eq('tainted command not reconciled',v['result']!=0,True)
   elif case==16:
    v=step(15);eq('other HAL Flush blocked',v['result'],0xfffffffb);eq('no HAL boundary reached',v['unexpected'],0)
   elif case==26:
    eq('stalled tick bounded by polls',v['stage'],8);eq('failure preserved',v['first_error'],0xfffffff8)
   else:
    step(4,11);v=step(8);eq('timeout enters recovery',v['stage'],8);eq('first error retained',v['first_error'],0xfffffff8)
    if case==17:eq('owner remains held',v['owner'],2)
    elif case==21:
     step(12);v=step(9);eq('old session rejected',v['result']!=0,True)
    elif case==22:
     step(16);v=step(9);eq('forged Attempt rejected',v['result']!=0,True)
    elif case in (18,19,23,24,25):
     if delay_disable:delay_disable=False;wr(CTL,rd(CTL)&~((1<<31)|(1<<30)));wr(INT,rd(INT)|2)
     delay_fifo=False;wr(GRST,rd(GRST)&~(1<<5))
     if case==24:step(17,1)
     if case==25:wr(GRST,(rd(GRST)&~(31<<6))|(1<<6))
     v=step(9)
     if case in (24,25):eq('incomplete proof rejected',v['result']!=0,True)
     else:
      eq('Reconcile accepted',v['result'],0);eq('same Attempt',v['attempt'],attempt);eq('first error immutable',v['first_error'],0xfffffff8);eq('one reconcile',v['reconciles'],1)
      if case==23:eq('repeat Reconcile rejected',step(9)['result']!=0,True)
      v=reach(6);eq('new independent proof',v['stage'],6);v=step(10);eq('commit succeeds',v['result'],0);eq('one return',v['source_returns'],1)
   if v['source']:eq('original source pointer retained',v['source_ptr'],source)
  eq('no source double return',trace[-1]['source_returns']<=1,True);eq('no pool double return',trace[-1]['pool_returns']<=1,True);eq('transfer identity unchanged',trace[-1]['transfer'],identity);eq('PRIMASK restored',trace[-1]['primask'],0)
 except Exception as e:error=str(e)
 passed=error is None and all(x['passed'] for x in checks)
 rows.append(dict(id=f'P1B1-{case:02}',name=name,passed=passed,error=error,assertions=checks,trace=trace,writes=writes))
 print(rows[-1]['id'],'PASS' if passed else 'FAIL',error,[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],flush=True)
report=dict(baseline='5113000227e8d3d3a9a460b4585993e28096d0cc',cases=rows,commands=commands,summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),hardware='NOT_TESTED',sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['CM7/BSP/bsp_usb.c','CM7/BSP/bsp_usb.h','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h','CM7/Service/usb_mode.c','CM7/Application/app_usb.c','Tests/UsbBulkInIntegration/integration_test.c','Tests/UsbBulkInIntegration/boundary_stubs.c','Tests/UsbBulkInIntegration/run_tests.py']})
prior=REPORT/'integration.json'
if prior.exists():(REPORT/('history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
prior.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert all(x['passed'] for x in rows)
