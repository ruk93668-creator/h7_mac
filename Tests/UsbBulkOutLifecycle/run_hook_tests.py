"""Execute actual BSP/Platform ARM code with explicit, fault-injectable USB MMIO."""
from pathlib import Path
import sys, subprocess, json, hashlib, struct, xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_PC,UC_ARM_REG_IPSR,UC_ARM_REG_PRIMASK
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC24/udlm-p1c1/hook';OUT.mkdir(parents=True,exist_ok=True)
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
for name in ['Tests/UsbBulkOutLifecycle/hal_hook_test.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c']:
 o=OUT/(Path(name).stem+'.o');run(args+['-c',ROOT/name,'-o',o]);objects.append(o)
run([cc/'armlink.exe',*objects,'--entry','Hook_Step','--no_scanlib','--scatter',ROOT/'Tests/EndpointLifecycle/test.sct','-o',axf])
with axf.open('rb') as f:
 elf=ELFFile(f);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]


from harness import VM,BASE,GINT,MASK,DCTL,DMASK,OMASK,IMASK
basefields='out in setup iso reset count2 ptr2 data2 rearm_data dctl mask raw ctl2 int2 last data1 count1 setup0 setup1 iso_pending ctl1 data0 callback_xfrc unexpected'.split()
hookfields='owner pio begin end copied denies status pre post result iso_request effective boundary invalid spare spare2 events cookie terminal_ends enabled'.split()
names=['reject partial hook table','bind complete table','deny detach while references held','detach only after owner release','valid 31-byte copy with balanced references','null destination denied before copy','overflow denied before copy','explicit copy veto drains only that packet','EP0 SETUP dedicated buffer','terminal facts precede W1C and suppress callback','ISO NAK request delegated','EPDISD cannot release NAK outside manager','effective NAK delegated without synchronous Abort','Reset invalidates before copy without releasing owner','Disconnect invalidates before copy','reject bind while global IRQ enabled','reject DMA binding','malformed SETUP denied before copy','callback rearm retains original event snapshot']
rows=[]
for case,name in enumerate(names,1):
 vm=VM(symbols,sections,'Hook_Step',{'g_c0':basefields,'g_hook':hookfields});checks=[];error=None
 def step(op,arg=0):return vm.step(op,arg)
 def eq(n,a,b):checks.append(dict(name=n,actual=a,expected=b,passed=a==b))
 try:
  step(0);vm.wr(BASE+0x40,0x4f54310a);vm.wr(MASK,(1<<4)|(1<<19)|(1<<7));vm.wr(DMASK,0x1ff0000);vm.wr(OMASK,11)
  if case==1:
   v=step(3)['g_hook'];eq('partial rejected',v['result'],1);eq('binding unchanged',v['enabled'],0)
  elif case in (16,17):
   if case==16:vm.wr(BASE+8,1)
   else:step(10)
   v=step(2)['g_hook'];eq('unsafe binding rejected',v['result'],1);eq('nil binding retained',v['enabled'],0)
  else:
   v=step(2)['g_hook'];eq('complete binding accepted',v['result'],0);eq('binding installed',v['enabled'],1)
   if case==3:v=step(5)['g_hook'];eq('detach BUSY',v['result'],2);eq('still bound',v['enabled'],1)
   elif case==4:step(6);v=step(5)['g_hook'];eq('safe detach',v['result'],0);eq('now nil',v['enabled'],0)
   elif case in (5,6,7,8,14,15):
    data=b'X'*(128 if case==7 else 31 if case==5 else 4)
    if case==6:step(9)
    if case==8:step(4,1)
    if case==14:vm.wr(MASK,vm.rd(MASK)|(1<<12));vm.wr(GINT,1<<12)
    if case==15:vm.wr(MASK,vm.rd(MASK)|(1<<2));vm.wr(GINT,1<<2);vm.wr(BASE+4,1<<2)
    vm.queue(2,data);v=step(1);h=v['g_hook'];b=v['g_c0']
    eq('one status observed',h['begin'],1);eq('one status end',h['end'],1);eq('references returned after copy/drain',h['pio'],0);eq('owner not retired by copy',h['owner'],1)
    eq('payload drained',len(vm.payload),0);eq('one status pop',sum(x['kind']=='pop' for x in vm.reads),1)
    if case==5:eq('valid byte count',b['count2'],31);eq('no deny',h['denies'],0)
    else:eq('no invalid buffer write',b['data2'],0xaaaaaaaa);eq('copy veto recorded',h['denies'],1)
    if case in (14,15):eq('boundary published',h['invalid'],1)
   elif case in (9,18):
    vm.queue(0,b'12345678' if case==9 else b'1234',6);v=step(1)
    eq('setup bytes copied only when valid',v['g_c0']['setup0'],0x34333231 if case==9 else 0);eq('setup ref returned',v['g_hook']['pio'],0)
   elif case in (10,12,19):
    if case==10:step(7)
    if case==19:step(18,1)
    vm.wr(GINT,1<<7);vm.wr(DCTL,8);vm.event(2,3 if case in (10,19) else 2);v=step(1)
    eq('full pre-W1C snapshot',v['g_hook']['pre']&3,3 if case in (10,19) else 2);eq('post-W1C retired',v['g_hook']['post']&3,0)
    eq('no implicit shared clear',vm.rd(GINT)&(1<<7),1<<7)
    if case==10:eq('ordinary callback vetoed',v['g_c0']['out'],0)
    if case==19:eq('normal callback rearmed',v['g_c0']['ptr2'],symbols['g_rearm'])
   elif case==11:
    step(11);vm.wr(BASE+0xb20,1<<31);vm.wr(MASK,vm.rd(MASK)|(1<<21));vm.wr(GINT,1<<21);v=step(1)
    eq('ISO request delivered once',v['g_hook']['iso_request'],1);eq('HAL did not issue SGONAK',any(int(x['address'],16)==DCTL and int(x['value'],16)&(1<<9) for x in vm.writes),False)
   elif case==13:
    vm.wr(GINT,1<<7);vm.wr(DCTL,8);step(12);v=step(1)
    eq('effective event delegated',v['g_hook']['effective'],1);eq('HAL not polling ISO Abort',vm.rd(BASE+0xb20)&(1<<30),0)
  eq('no fixture LPM boundary invoked',vm.step(99)['g_c0']['unexpected'],0)
 except Exception as e:error=str(e)
 passed=error is None and all(c['passed'] for c in checks);rows.append(dict(id=f'HOOK-{case:02}',name=name,passed=passed,error=error,assertions=checks,trace=vm.trace,reads=vm.reads,writes=vm.writes));print(rows[-1]['id'],'PASS' if passed else 'FAIL',error,[x for x in checks if not x['passed']],flush=True)
report=dict(baseline='3305d4a729e965247b33c95e5558eac576f1c6ef',layer='real HAL hooks; no BSP Stop involved',cases=rows,commands=commands,summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),assertions=sum(len(x['assertions']) for x in rows)),sources={n:hashlib.sha256((ROOT/n).read_bytes()).hexdigest() for n in ['Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c','Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_pcd.h','Tests/UsbBulkOutLifecycle/hal_hook_test.c','Tests/UsbBulkOutLifecycle/harness.py','Tests/UsbBulkOutLifecycle/run_hook_tests.py']},hardware='NOT_TESTED')
prior=REPORT/'hooks.json'
if prior.exists():(REPORT/('hooks-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
prior.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert all(x['passed'] for x in rows)
