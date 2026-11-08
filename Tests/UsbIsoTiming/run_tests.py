"""ARMCC5 + Unicorn tests of the actual timing recorder; no firmware flashing."""
from pathlib import Path
import subprocess,json,struct,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC,UC_ARM_REG_IPSR
OUT=ROOT/'output/UsbIsoTiming';OUT.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');commands=[];rows=[]
def run(cmd):
 p=subprocess.run(list(map(str,cmd)),capture_output=True);out=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(command=subprocess.list2cmdline(list(map(str,cmd))),exit_code=p.returncode,output=out));(HERE/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 if p.returncode or 'warning:' in out.lower():raise RuntimeError(out)
names=['initialization','inactive_no_records','task_record','irq_record','manual_freeze','capacity_freeze','rearm_after_ready','active_rearm_rejected','invalid_request','primask_restore','cache_flush_task_only','no_cycle_counter','zero_cpu_frequency','stop_idle','capacity_watch_cannot_expand','cycle_wrap','pending_writer_delays_ready','timer_disabled_marks_invalid','pending_update_detected','sleep_clock_disabled_marks_invalid']
base=[cc,'--cpu','Cortex-M7','--c99','--split_sections','-DCORE_CM7','-DSTM32H747xx']
for inc in [HERE,ROOT/'Drivers/CMSIS/Include',ROOT/'Drivers/CMSIS/Device/ST/STM32H7xx/Include']:base+=['-I',inc]
for enabled in [1,0]:
 for opt in ['O0','O2']:
  obj=OUT/('trace_'+str(enabled)+'_'+opt+'.o');axf=obj.with_suffix('.axf')
  run(base+['-DAPP_USB_ISO_TIMING_TEST_ENABLED='+str(enabled),'-'+opt,'-c',HERE/'trace_test.c','-o',obj]);run([cc.with_name('armlink.exe'),obj,'--entry','TraceTest_Run','--scatter',HERE/'test.sct','-o',axf])
  with axf.open('rb') as f:
   e=ELFFile(f);entry=e.header['e_entry'];sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']];sy={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
  assert ('g_usb_iso_timing' in sy)==bool(enabled)
  for case,name in enumerate(names if enabled else ['disabled_no_resources']):
   u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
   for a,n in [(0x08000000,0x100000),(0x20000000,0x20000),(0x24000000,0x80000),(0x40080000,0x10000),(0x40001000,0x1000),(0x58024000,0x1000),(0xE0000000,0x10000),(0x01000000,0x1000)]:u.mem_map(a,n)
   for a,b in sections:u.mem_write(a,b)
   cache_writes=[0];bad_writes=[]
   def code_hook(uc,address,size,data):
    if address==(sy.get('TraceTest_Context',0)&~1):uc.reg_write(UC_ARM_REG_IPSR,uc.reg_read(UC_ARM_REG_R0))
    ctrl=int.from_bytes(uc.mem_read(0xe0001000,4),'little')
    if ctrl&1:
     value=int.from_bytes(uc.mem_read(0xe0001004,4),'little');uc.mem_write(0xe0001004,((value+1)&0xffffffff).to_bytes(4,'little'))
    if 'uwTick' in sy:
     counter=int.from_bytes(uc.mem_read(0x40001024,4),'little')+1
     if counter>=1000:
      counter=0;tick=int.from_bytes(uc.mem_read(sy['uwTick'],4),'little');uc.mem_write(sy['uwTick'],((tick+1)&0xffffffff).to_bytes(4,'little'))
     uc.mem_write(0x40001024,counter.to_bytes(4,'little'))
   def mem_hook(uc,access,address,size,value,data):
    if address==0xe000ef68:cache_writes[0]+=1
    if 0x24020000<=address<0x24028000 and address+size>0x24026000:bad_writes.append(hex(address))
   u.hook_add(UC_HOOK_CODE,code_hook);u.hook_add(UC_HOOK_MEM_WRITE,mem_hook)
   u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
   error=None
   try:u.emu_start(entry|1,0x01000000,count=8000000)
   except Exception as exc:error=str(exc)
   line=int.from_bytes(u.mem_read(sy['g_trace_test_line'],4),'little') if 'g_trace_test_line' in sy else 0;checks=int.from_bytes(u.mem_read(sy['g_trace_test_checks'],4),'little')
   ok=not error and not line and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0 and not bad_writes
   if enabled and case==10:ok=ok and cache_writes[0]>=768
   rows.append(dict(enabled=enabled,optimization=opt,case=case,name=name,result='PASS' if ok else 'FAIL',checks=checks,line=line,error=error,cache_clean_writes=cache_writes[0],out_of_bounds=bad_writes))
   if not ok:print(rows[-1],flush=True)
  print(enabled,opt,flush=True)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows))
(HERE/'results.json').write_text(json.dumps(dict(summary=summary,cases=rows,hardware_verified=False),indent=2),encoding='utf-8');print(summary)
raise SystemExit(bool(summary['failed']))
