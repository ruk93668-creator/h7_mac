"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1c/hal-evidence';OUT.mkdir(parents=True,exist_ok=True)
REPORT=ROOT/'Tests/UsbBulkOut/reports';REPORT.mkdir(parents=True,exist_ok=True)
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
for name in ['Tests/UsbBulkOut/hal_out_evidence.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c']:
 o=OUT/(Path(name).stem+'.o');run(args+['-c',ROOT/name,'-o',o]);objects.append(o)
run([cc/'armlink.exe',*objects,'--entry','C0_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]

BASE=0x40080000;GRST=BASE+0x10;GINT=BASE+0x14;MASK=BASE+0x18;PEEK=BASE+0x1c;POP=BASE+0x20;DCTL=BASE+0x804;DAINT=BASE+0x818;DMASK=BASE+0x81c;OMASK=BASE+0x814;IMASK=BASE+0x810;FIFO=BASE+0x1000
fields='out_calls in_calls setup_calls iso_calls reset_calls count2 ptr2 data2 rearm_data dctl gintmask gintsts ctl2 int2 last_read data1 count1 setup0 setup1 iso_pending ctl1 data0 callback_xfrc unexpected'.split()
names=['GRXSTSR peek is non-destructive','GRXSTSP reads pop distinct statuses','HAL RX copy after EPENA clear','masking target completion does not mask RX writes','EP0 SETUP data preserved and dispatched','EP1 and EP2 RX entries share one consumer','unrelated OUT EPDISD clears effective Global NAK','target EPDISD has no ordinary cancel callback','HAL GONAKEFF handler masks shared interrupt','ISO OUT requests shared Global NAK','GONAKEFF marker must be popped','masked RXFLVL prevents NAK marker progress','GONAKEFF does not exclude later EP0 SETUP','OUT XFRC callback sees current context','XFRC callback rearm precedes cached EPDISD clear','IN completes while OUT status pending','pending raw XFRC is W1C before callback','old raw XFRC has no transfer generation','queued data is copied before simultaneous reset handling','GONAKEFF independently aborts ISO then clears shared NAK','LL Abort failure retains active hardware and Buffer','normal OUT completion with 300A core model','one-byte RX tail preserves adjacent buffer bytes','31-byte RX packet consumes exactly eight FIFO words']
rows=[]
for case,name in enumerate(names,1):
 cpu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000),(BASE,0x40000),(0xe0000000,0x100000)]:cpu.mem_map(a,n)
 for a,data in sections:cpu.mem_write(a,data)
 def rd(a):return struct.unpack('<I',cpu.mem_read(a,4))[0]
 def wr(a,v):cpu.mem_write(a,struct.pack('<I',v&0xffffffff))
 q=[];payload=[];pending=[];trace=[];writes=[];reads=[];checks=[];pops=[]
 def queue(ep,kind,words=()):q.append((ep|(4*len(words)<<4)|(kind<<17),list(words)))
 def levels():
  out=sum((1<<ep) for ep in range(9) if rd(BASE+0xb08+ep*0x20)&rd(OMASK))
  ins=sum((1<<ep) for ep in range(9) if rd(BASE+0x908+ep*0x20)&rd(IMASK))
  wr(DAINT,out<<16|ins)
  flags=rd(GINT)&~((1<<4)|(1<<18)|(1<<19))
  if q:flags|=1<<4
  if out&(rd(DMASK)>>16):flags|=1<<19
  if ins&rd(DMASK):flags|=1<<18
  wr(GINT,flags)
 def memread(uc,access,a,size,value,ctx):
  if a in (GINT,DAINT):levels()
  if a==PEEK:wr(a,q[0][0] if q else 0);reads.append(dict(kind='peek',value=rd(a)))
  if a==POP:
   if not q:raise RuntimeError('illegal empty status pop')
   if payload:raise RuntimeError('status popped before previous payload consumed')
   status,words=q.pop(0);wr(a,status);payload.extend(words);pops.append(status);reads.append(dict(kind='pop',value=status))
   if (status>>17)&15==1:wr(GINT,rd(GINT)|(1<<7));wr(DCTL,rd(DCTL)|8)
   levels()
  if a==FIFO:
   if not payload:raise RuntimeError('FIFO data read with no payload')
   wr(a,payload.pop(0));reads.append(dict(kind='data',value=rd(a)))
  if any(a==BASE+0xb00+ep*0x20 for ep in range(9)) and case!=21 and rd(a)&(1<<30):
   wr(a,rd(a)&~((1<<31)|(1<<30)));wr(a+8,rd(a+8)|2)
  if a==GRST:wr(a,(rd(a)|(1<<31))&~((1<<5)|(1<<4)))
 def memwrite(uc,access,a,size,v,ctx):
  writes.append(dict(address=hex(a),value=hex(v),pc=hex(cpu.reg_read(UC_ARM_REG_PC))))
  if a==GINT or any(a==BASE+off+ep*0x20 for off in (0x908,0xb08) for ep in range(9)):pending.append((a,rd(a)&~v))
  if a==DCTL:
   value=v&~((1<<9)|(1<<10))
   if v&(1<<10):value&=~8;pending.append((GINT,rd(GINT)&~(1<<7)))
   pending.append((a,value))
 from unicorn import UC_HOOK_CODE
 def code(uc,a,size,ctx):
  while pending:
   x,v=pending.pop(0);wr(x,v)
 cpu.hook_add(UC_HOOK_MEM_READ,memread,begin=BASE,end=BASE+0x3ffff);cpu.hook_add(UC_HOOK_MEM_WRITE,memwrite,begin=BASE,end=BASE+0x3ffff);cpu.hook_add(UC_HOOK_CODE,code)
 def step(op,arg=0):
  cpu.reg_write(UC_ARM_REG_SP,0x2003fff0);cpu.reg_write(UC_ARM_REG_LR,0x01000001);cpu.reg_write(UC_ARM_REG_R0,op);cpu.reg_write(UC_ARM_REG_R1,arg)
  cpu.emu_start(symbols['C0_Step']|1,0x01000000,count=1500000,timeout=3000000)
  assert cpu.reg_read(UC_ARM_REG_PC)==0x01000000,'did not return'
  v=dict(zip(fields,struct.unpack('<24I',cpu.mem_read(symbols['g_c0'],96))));v.update(op=op,pops=len(pops),queued=len(q));trace.append(v);return v
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 def interrupt(ep,bits):wr(BASE+0xb08+ep*0x20,rd(BASE+0xb08+ep*0x20)|bits)
 def active_nak():wr(GINT,rd(GINT)|(1<<7));wr(DCTL,8)
 error=None
 try:
  step(0);wr(BASE+0x40,0x4f54300a if case==22 else 0x4f54310a);wr(MASK,(1<<4)|(1<<19)|(1<<18)|(1<<7));wr(DMASK,0x1ff01ff);wr(OMASK,11);wr(IMASK,3)
  if case==23:
   q.append((2|(1<<4)|(2<<17),[0x44332211]));v=step(1)
   eq('one byte copied, neighbours unchanged',v['data2'],0xaaaaaa11);eq('count is exact byte count',v['count2'],1);eq('one destructive payload word read',sum(x['kind']=='data' for x in reads),1)
  elif case==24:
   q.append((2|(31<<4)|(2<<17),[0x44332211]*8));v=step(1);base=symbols['g_buffers']+128
   eq('31-byte packet count exact',v['count2'],31);eq('byte after packet untouched',cpu.mem_read(base+31,1)[0],0xaa);eq('last payload byte copied',cpu.mem_read(base+30,1)[0],0x33);eq('exactly eight destructive payload reads',sum(x['kind']=='data' for x in reads),8)
  elif case==20:
   step(6);step(7);active_nak();v=step(1)
   eq('ISO disable submitted independently',v['ctl1']&(1<<31),0);eq('ISO callback not cancellation request',v['iso_calls'],0)
   v=step(1);eq('later EPDISD clears global lease',v['gintsts']&(1<<7),0);eq('ISO incomplete callback delivered later',v['iso_calls'],1)
  elif case==21:
   wr(BASE+0xb40,1<<31);v=step(8)
   eq('real LL reports HAL_ERROR after iteration limit',v['last_read'],1);eq('EPENA remains active',v['ctl2']&(1<<31),1<<31);eq('old buffer retained',v['data2'],0xaaaaaaaa);eq('no completion callback',v['out_calls'],0)
  elif case==22:
   interrupt(2,1);v=step(1);eq('300A non-DMA completion callback',v['out_calls'],1);eq('old XFRC acknowledged',v['int2']&1,0)
  elif case in (1,2):
   queue(0,1);queue(2,3)
   if case==1:
    a=step(2);b=step(2);eq('same peek value',a['last_read'],b['last_read']);eq('no destructive reads',len(pops),0);eq('both statuses retained',len(q),2)
   else:
    a=step(3);b=step(3);eq('different status values',a['last_read']!=b['last_read'],True);eq('each status consumed exactly once',len(pops),2)
  elif case in (3,4,19):
   queue(2,2,[0x44332211]);wr(BASE+0xb40,0)
   if case==4:wr(DMASK,rd(DMASK)&~(1<<18))
   if case==19:wr(MASK,rd(MASK)|(1<<12));wr(GINT,rd(GINT)|(1<<12))
   v=step(1);eq('unchanged HAL writes queued data',v['data2'],0x44332211);eq('byte count updated',v['count2'],4);eq('EPENA did not authorize release',v['ctl2']&(1<<31),0)
   if case==19:
    rxpc=next(i for i,x in enumerate(writes) if int(x['address'],16)==MASK and not int(x['value'],16)&(1<<4));resetpc=next(i for i,x in enumerate(writes) if int(x['address'],16)==GRST);eq('RX branch precedes reset flush',rxpc<resetpc,True)
  elif case==5:
   queue(0,6,[0x12345678,0x9abcdef0]);step(1);interrupt(0,8);v=step(1)
   eq('SETUP bytes retained',v['setup0'],0x12345678);eq('second SETUP word',v['setup1'],0x9abcdef0);eq('SETUP callback invoked',v['setup_calls'],1);eq('EP0 data buffer untouched',v['data0'],0xaaaaaaaa)
  elif case==6:
   queue(1,2,[0x11111111]);queue(2,2,[0x22222222]);a=step(1);b=step(1)
   eq('one pop per IRQ',a['pops'],1);eq('EP1 receives its data',b['data1'],0x11111111);eq('EP2 receives its data',b['data2'],0x22222222);eq('shared queue drained',len(q),0)
  elif case in (7,8):
   active_nak();interrupt(1 if case==7 else 2,2);v=step(1)
   eq('HAL clears shared effective NAK',v['gintsts']&(1<<7),0);eq('clear command really issued',any(int(w['address'],16)==DCTL and int(w['value'],16)&(1<<10) for w in writes),True);eq('no ordinary cancel callback',v['out_calls'],0);eq('no ISO callback for Bulk',v['iso_calls'],0)
  elif case==9:
   active_nak();v=step(1);eq('HAL masks GONAKEFFM',v['gintmask']&(1<<7),0);eq('no request identity delivered',v['out_calls'],0)
  elif case==10:
   step(6);wr(MASK,rd(MASK)|(1<<21));wr(GINT,rd(GINT)|(1<<21));v=step(1)
   eq('ISO has its own pending state',v['iso_pending'],1);eq('ISO requests GONAKEFFM',v['gintmask']&(1<<7),1<<7);eq('ISO issues shared SGONAK',any(int(w['address'],16)==DCTL and int(w['value'],16)&(1<<9) for w in writes),True)
  elif case in (11,12,13):
   step(9);queue(2,2,[0x07070707]);queue(0,1)
   if case==12:
    wr(MASK,rd(MASK)&~(1<<4));v=step(1);eq('no pop while RX masked',len(pops),0);eq('NAK effective is not request-only',v['gintsts']&(1<<7),0)
   else:
    if case==13:queue(0,6,[0xabcddcba,0x10203040])
    a=step(1);eq('before marker no effective NAK',a['gintsts']&(1<<7),0);b=step(1);eq('marker pop sets effective NAK',b['gintsts']&(1<<7),1<<7)
    if case==13:
     eq('SETUP still queued after barrier',len(q),1);v=step(1);eq('later SETUP not discarded',v['setup0'],0xabcddcba)
  elif case in (14,15,17,18):
   if case==15:step(5,1)
   interrupt(2,3 if case==15 else 1)
   if case==15:active_nak()
   v=step(1);eq('OUT normal callback delivered',v['out_calls'],1);eq('old raw XFRC acknowledged',v['int2']&1,0)
   if case==17:eq('XFRC clear observed inside callback',v['callback_xfrc'],0)
   if case==15:
    eq('callback rearm uses new buffer',v['ptr2'],symbols['g_rearm']);eq('same cached EPDISD clears NAK',v['gintsts']&(1<<7),0)
   if case==18:
    step(4);interrupt(2,1);v=step(1);eq('late raw flag dispatched again without identity',v['out_calls'],2)
  elif case==16:
   queue(2,2,[0xeeeeeeee]);wr(BASE+0x928,1);v=step(1)
   eq('IN callback still delivered',v['in_calls'],1);eq('OUT copied independently',v['data2'],0xeeeeeeee)
  eq('no unmodelled LPM boundary used',trace[-1]['unexpected'],0)
 except Exception as e:error=str(e)+f' pc={cpu.reg_read(UC_ARM_REG_PC):#x}'
 passed=error is None and all(x['passed'] for x in checks)
 rows.append(dict(id=f'C0-{case:02}',name=name,passed=passed,error=error,assertions=checks,trace=trace,mmio_reads=reads,mmio_writes=writes,status='VERIFIED software branch only; NOT_TESTED silicon; NOT an OUT Stop acceptance'))
 print(rows[-1]['id'],'PASS' if passed else 'FAIL',error,[(x['name'],x['actual'],x['expected']) for x in checks if not x['passed']],flush=True)
report=dict(baseline='03a5cb1793e35ec23728f3a6ebbdc132277ed597',production_modified=False,actual_hal_and_ll=True,cases=rows,commands=commands,summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),model='Slave/PIO, destructive status/data queues and W1C. NAK marker effect follows RM0399 p2909. Injected register combinations are software probes, not physical occurrence proofs.',sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c','Tests/UsbBulkOut/hal_out_evidence.c','Tests/UsbBulkOut/run_hal_evidence.py']})
prior=REPORT/'hal_evidence.json'
if prior.exists():(REPORT/('hal-evidence-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
prior.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert all(x['passed'] for x in rows)
