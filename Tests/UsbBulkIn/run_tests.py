"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1b/specialist';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Tests/UsbBulkIn/reports';REPORT.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin')
target=next(t for t in ET.parse(ROOT/'MDK-ARM/h7_mac.uvprojx').findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
args=[str(cc/'armcc.exe'),'--cpu','Cortex-M7','--c99','--split_sections','-O0','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER','--preinclude',str(ROOT/'CM7/BSP/bsp_usb_hal_config.h')]
for inc in target.findtext('TargetOption/TargetArmAds/Cads/VariousControls/IncludePath').split(';'):args+=['-I',str((ROOT/'MDK-ARM'/inc).resolve())]
commands=[]
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True);s=(p.stdout+p.stderr).decode('utf-8',errors='replace');commands.append(dict(argv=list(map(str,cmd)),exit=p.returncode,output=s));print(s)
 if p.returncode or 'warning:' in s.lower():raise RuntimeError(s)
obj=OUT/'bulk_in_test.o';axf=OUT/'bulk_in_test.axf'
run(args+['-c',ROOT/'Tests/UsbBulkIn/bulk_in_test.c','-o',obj])
stub=OUT/'boundary_stubs.o'
run(args+['-c',ROOT/'Tests/UsbBulkIn/boundary_stubs.c','-o',stub])
run([cc/'armlink.exe',obj,stub,'--entry','BulkInTest_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
BASE=0x40080000;CTL=BASE+0x940;INT=CTL+8;GRST=BASE+0x10
names=['idle endpoint','normal XFRC before stop','processed intermediate XFRC','last XFRC undelivered','intermediate XFRC undelivered','XFRC and EPDISD race','NAK timeout','disable timeout','TX flush timeout','repeated Begin','repeated Commit','late raw event before new arm','old transfer identity after new arm','EP1 normal during EP2 stop','reset during stop','disconnect during stop','PIO visitor outstanding','no early source release','identity exhaustion','stop from ISR rejected','transfer identity exhaustion','forged endpoint generation','late callback before retirement','IRQ while GRSTCTL owned','restore originally masked target','EPENA clear without terminal evidence','NAK level does not masquerade as old XFRC','common mask would expose unretired NAK','unrequested disable event','legacy flush fenced','endpoint identity exhaustion','premature commit rejected','unknown failed submission retains ownership','continuation failure retains busy','controller generation exhausted','session generation exhausted','original CPU mask preserved','Task Stop coordinator starts bounded transaction']
fields=['status','state','stage','result','transfer','attempt','source_owned','pool_owned','queued','completed','packet_length','events','packet_completions','source_returns','pool_returns','class_completions','transmits','daint_mask','empty_mask','fifo_complete','events_retired','completed_undelivered','platform_busy','nvic_changes','pio_users','error','controller','session','endpoint_generation','current_session','ep1_state','source_pointer','unexpected_boundary','fifo_result','class_memory_owned','class_state','device_fault','last_error']
rows=[]
for case,name in enumerate(names,1):
 cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(BASE,0x40000),(0xe0000000,0x100000)]:cpu.mem_map(a,n)
 for a,data in sections:cpu.mem_write(a,data)
 def rd(a):return struct.unpack('<I',cpu.mem_read(a,4))[0]
 def wr(a,v):cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
 pending=[];writes=[];trace=[];checks=[]
 def check(label,value,expected):checks.append(dict(name=label,actual=value,expected=expected,passed=value==expected))
 def memread(uc,access,a,size,value,ctx):
  if a==CTL:
   v=rd(CTL)
   if v&(1<<27):
    v=(v&~(1<<27))|(1<<17);wr(CTL,v)
    if case!=7:wr(INT,rd(INT)|(1<<6))
   if v&(1<<30) and case!=8:
    wr(CTL,v&~((1<<31)|(1<<30)));wr(INT,rd(INT)|2|(1 if case==6 else 0))
  if a==GRST:
   v=rd(GRST)|(1<<31)
   if case!=9:v &= ~(1<<5)
   wr(GRST,v)
 def memwrite(uc,access,a,size,value,ctx):
  if a in (INT,BASE+0x928):pending.append((a,rd(a)&~value))
  if BASE<=a<BASE+0x40000:writes.append(dict(address=hex(a),value=hex(value)))
 cpu.hook_add(UC_HOOK_MEM_READ,memread,begin=BASE,end=BASE+0x3ffff)
 cpu.hook_add(UC_HOOK_MEM_WRITE,memwrite,begin=BASE,end=BASE+0x3ffff)
 # W1C side effects must apply before the next CPU instruction, never treat MMIO as normal RAM.
 from unicorn import UC_HOOK_CODE
 def code(uc,a,size,ctx):
  while pending:
   x,v=pending.pop(0);wr(x,v)
 cpu.hook_add(UC_HOOK_CODE,code)
 def invalid(uc,access,a,size,value,ctx):
  raise RuntimeError(f'unmapped address={a:#x} size={size} pc={cpu.reg_read(UC_ARM_REG_PC):#x}')
 cpu.hook_add(UC_HOOK_MEM_INVALID,invalid)
 def step(op):
  cpu.reg_write(UC_ARM_REG_SP,0x2003fff0);cpu.reg_write(UC_ARM_REG_LR,0x01000001);cpu.reg_write(UC_ARM_REG_R0,op);cpu.reg_write(UC_ARM_REG_R1,case)
  cpu.emu_start(symbols['BulkInTest_Step']|1,0x01000000,count=200000,timeout=3000000)
  assert cpu.reg_read(UC_ARM_REG_PC)==0x01000000,'unbounded or trapped execution'
  values=struct.unpack('<38I',cpu.mem_read(symbols['g_bulk_observation'],152));snap=dict(zip(fields,values));snap['op']=op;snap['primask']=cpu.reg_read(UC_ARM_REG_PRIMASK);snap['registers']={'DIEPCTL2':rd(CTL),'DIEPINT2':rd(INT),'GRSTCTL':rd(GRST)};trace.append(snap);return snap
 error=None
 try:
  v=step(0);check('initial submission valid',v['status'],0);original_daint=v['daint_mask'];original_empty=v['empty_mask']
  if case in (2,3):
   wr(CTL,rd(CTL)&~(1<<31));wr(INT,0);v=step(2)
  if case in (4,5):wr(CTL,rd(CTL)&~(1<<31));wr(INT,1)
  if case==19:step(14)
  if case==26:wr(CTL,rd(CTL)&~(1<<31))
  if case==29:wr(INT,2)
  if case==34:
   wr(CTL,rd(CTL)&~(1<<31));wr(INT,0);step(24)
  if case==38:
   v=step(26);check('Task Stop pending is BUSY',v['status']!=0,True);check('Task Stop already fenced target',v['daint_mask'],original_daint&~4);check('Class not released',v['class_memory_owned'],1);check('Task Stop has not disabled NVIC',v['nvic_changes'],0)
  if case==37:cpu.reg_write(UC_ARM_REG_PRIMASK,1)
  if case==20:cpu.reg_write(UC_ARM_REG_IPSR,16)
  v=step(1)
  if case==20:
   check('ISR rejected',v['status']!=0,True);check('no target mask changed',v['daint_mask'],0x10007);check('source retained',v['source_owned'],1)
  elif case==19:
   check('attempt exhaustion rejected',v['status']!=0,True)
  else:
   check('begin accepted',v['status'],0);attempt=v['attempt'];identity=v['transfer']
   check('only target DAINT bit masked',v['daint_mask'],original_daint&~4)
   check('only target TXFE bit masked',v['empty_mask'],original_empty&~4)
   check('no NVIC change by begin',v['nvic_changes'],0 if case not in (2,3) else 0)
   if case==10:check('same StopAttempt',step(12)['attempt'],attempt)
   if case==17:step(10)
   if case==22:
    check('forged generation rejected',step(7)['status']!=0,True)
   if case==23:step(11)
   if case==30:check('legacy flush blocked without HAL',step(19)['status']!=0,True)
   if case==32:check('early commit refused',step(4)['status']!=0,True)
   if case==15:step(8)
   if case==16:step(9)
   if case==14:
    before=v['daint_mask'];v=step(13);check('EP1 completion does not alter EP2 mask',v['daint_mask'],before);check('EP1 returned idle',v['ep1_state'],1)
   for _ in range(12):
    v=step(3)
    if case==24 and v['stage']==4:
     step(18);v=step(3);break
    if v['stage'] in (6,8):break
   if case in (7,8,9,17):step(5);v=step(3)
   if case==28:
    check('proof was ready before mask conflict',v['stage'],6);wr(BASE+0x810,1<<6);v=step(4);check('unsafe restore rejected',v['status']!=0,True)
   if case in (15,16):
    check('Class retained across lifecycle event',v['class_memory_owned'],1);check('controlled fault requested',v['device_fault'],1)
   if case in (7,8,9,15,16,17,23,24,26,28,29,32,34):
    check('recovery state',v['state'],5);check('source retained',v['source_owned'],1);check('pool retained',v['pool_owned'],1)
    check('no false release',v['source_returns'],0);check('busy retained',v['platform_busy'],1)
   else:
    check('proof reached',v['stage'],6);check('FIFO proof',v['fifo_complete'],1);check('events retired',v['events_retired'],1)
    if case not in (1,2):check('source before commit',v['source_owned'],1)
    if case==3:check('completed excludes next queued packet',v['completed'],64);check('queued includes next packet',v['queued'],128)
    if case==4:check('complete undelivered',v['completed_undelivered'],1);check('no ordinary callback',v['class_completions'],0)
    if case==5:check('mid packet completion',v['completed'],64);check('no automatic continuation',v['transmits'],1)
    if case==6:check('both hardware events saved',v['events']&3,3);check('packet settled once',v['packet_completions'],1)
    v=step(4);check('commit succeeds',v['status'],0);check('platform busy returned',v['platform_busy'],0)
    check('source returned',v['source_owned'],0);check('pool returned',v['pool_owned'],0)
    check('source returned once',v['source_returns'],0 if case==1 else 1)
    check('original target DAINT restored',v['daint_mask'],original_daint);check('original empty mask restored',v['empty_mask'],original_empty)
    check('Stop did not change NVIC',v['nvic_changes'],0)
    if case==33:
     v=step(21);check('submission failure reported',v['status']!=0,True);check('failed submit busy retained',v['platform_busy'],1);check('failed submit source retained',v['source_owned'],1);check('failed submit pool retained',v['pool_owned'],1)
    if case==38:check('Task helper sees retired resource',step(25)['status'],0)
    if case==35:check('controller exhaustion rejected',step(22)['status']!=0,True)
    if case==36:check('session exhaustion rejected',step(23)['status']!=0,True)
    if case==27:
     wr(INT,(1<<6)|(1<<13));v=step(6);check('NAK level permits current transfer',v['status'],0)
    if case==31:check('endpoint generation exhaustion refused',step(20)['status']!=0,True)
    if case==21:
     step(15);v=step(6);check('transfer counter exhaustion rejects new transfer',v['status']!=0,True);check('no new pool reference',v['pool_owned'],0)
    if case==11:
     v=step(4);check('repeat commit idempotent',v['status'],0);check('no repeated source return',v['source_returns'],1)
    if case==12:
     wr(INT,1);v=step(6);check('late raw event prevents rearm',v['status']!=0,True);check('old event cannot send next packet',v['transmits'],1)
    if case==13:
     v=step(6);check('new transfer accepted',v['status'],0);check('new transfer identity',v['transfer']!=identity,True)
     v=step(16);check('old commit rejected',v['status']!=0,True);check('new source retained',v['source_owned'],1)
 except Exception as exc:error=str(exc)+f' pc={cpu.reg_read(UC_ARM_REG_PC):#x} sp={cpu.reg_read(UC_ARM_REG_SP):#x} last_writes={writes[-3:]}'
 if trace:
  check('CPU mask restored to caller value',trace[-1]['primask'],1 if case==37 else 0)
  check('source not returned twice',trace[-1]['source_returns']<=1,True)
  check('pool not returned twice',trace[-1]['pool_returns']<=1,True)
  check('controller identity retained',trace[-1]['controller'],1)
  check('endpoint identity retained',trace[-1]['endpoint_generation'],1)
  check('no unexpected hardware/core boundary',trace[-1]['unexpected_boundary'],1 if case in (15,24) else 0)
 rows.append(dict(id=f'BI{case:02}',name=name,passed=error is None and all(c['passed'] for c in checks),error=error,assertions=checks,trace=trace,mmio_writes=writes,model='PIO only; per-read hardware progress; W1C emulated; no silicon/timing proof'))
 print(rows[-1]['id'],'PASS' if rows[-1]['passed'] else 'FAIL',[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],error,flush=True)
report=dict(baseline='5a27af621c219ead2bc680858efd4aeb17e110a4',commands=commands,cases=rows,summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),failed=sum(not x['passed'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['CM7/BSP/bsp_usb.c','CM7/Platform/usb_device.c','Tests/UsbBulkIn/bulk_in_test.c']},silicon='NOT_TESTED')
prior=REPORT/'specialist.json'
if prior.exists():
 old=prior.read_bytes();(REPORT/('specialist-history-'+hashlib.sha256(old).hexdigest()[:10]+'.json')).write_bytes(old)
(REPORT/'specialist.json').write_bytes(json.dumps(report,ensure_ascii=False,indent=2).encode())
assert all(x['passed'] for x in rows)
