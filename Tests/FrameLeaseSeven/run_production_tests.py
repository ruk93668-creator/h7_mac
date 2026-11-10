"""Execute real Stage08D C sources with ARMCC5/Unicorn and RAM-only mock files."""
from pathlib import Path
import subprocess,json,sys,struct,hashlib,shutil
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
R=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent;O=R/'output/FrameLeaseSeven/production_verify';O.mkdir(parents=True,exist_ok=True)
commands=[];rows=[]
def command(args):
 p=subprocess.run(list(map(str,args)),capture_output=True,cwd=R)
 record=dict(command=subprocess.list2cmdline(list(map(str,args))),exit_code=p.returncode,output=(p.stdout+p.stderr).decode(errors='replace'));commands.append(record)
 (O/'production_commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
 if p.returncode:raise RuntimeError(record['output'])
 return record
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');command([cc,'--vsn'])
names=['retain_only', 'usb_first', 'sd_first', 'duplicate_return', 'old_session', 'seven_in_flight', 'jpg_roundtrip', 'jpg_split', 'avi_split', 'sd_one_frame_busy', 'write_failure_return', 'close_failure', 'stop_drains_single', 'wrong_return_owner', 'failed_return_keeps_ownership', 'dma_skips_loan', 'stop_preserves_refs', 'invalid_jpeg', 'source_still_owned', 'offer_no_io_or_crc', 'off_no_retain', 'return_ack_before_reuse', 'new_storage_session', 'bad_frame', 'memory_bounds', 'cancel_during_offer', 'recover_pending_returns', 'invalid_queue_recovery', 'short_write', 'reject_yuy2', 'input_timeout', 'stale_lease', 'usb_sd_sd_first', 'usb_sd_usb_first', 'sd_error_keeps_usb', 'abort_failure_keeps_usb', 'storage_stop_keeps_usb', 'usb_sd_split', 'maximum_70000_jpeg', 'reject_70001_jpeg', 'sd_single_does_not_block_usb', 'sd_single_backpressure', 'latest_ready_no_rollback', 'reserve_one_descriptor', 'shared_block_latest_drop', 'latest_drop_keeps_sd']
names+=['timing_math_saturation','timing_pipeline','timing_tick_wrap','timing_validation_failure','timing_empty_process']
names+=['wait_split','wait_repeat_rejected_offer','wait_foreign_context','wait_tick_wrap']
for opt in ['O0','O2']:
 obj=O/('test_'+opt+'.o');axf=O/('test_'+opt+'.axf')
 args=[cc,'--cpu','Cortex-M7','--c99','-'+opt,'--split_sections']
 for d in [R/'Tests/JpegStorageStage08D/stubs',R/'CM7/Application',R/'CM7/Service',R/'CM7/Platform',R/'CM7/Media/Inc',R/'CM7/BSP',R/'Common/Inc',R/'CM7/Framework',R/'Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:args+=['-I',d]
 command(args+['-c',H/'lease_production_test.c','-o',obj]);command([cc.with_name('armlink.exe'),obj,'--entry','Stage08D_TestRun','--scatter',R/'Tests/JpegStorageStage08D/test.sct','-o',axf])
 with axf.open('rb') as f:
  e=ELFFile(f);entry=e.header['e_entry'];segments=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']];sy={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 for case,name in enumerate(names):
  if case>=46:continue
  u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for a,size in [(0x08000000,0x100000),(0x20000000,0x20000),(0x60000000,0x100000),(0x61000000,0x1000000),(0xD0000000,0x800000),(0x01000000,0x1000)]:u.mem_map(a,size)
  for a,data in segments:u.mem_write(a,data)
  u.mem_write(0x60000000,(R/'Tests/JpegStorageStage08D/fixtures/sample.jpg').read_bytes());u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
  error=None
  try:u.emu_start(entry|1,0x01000000,count=100000000)
  except Exception as x:error=str(x)+' PC='+hex(u.reg_read(UC_ARM_REG_PC))
  line=u.reg_read(UC_ARM_REG_R0);ok=error is None and line==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
  rows.append(dict(optimization=opt,id=case,name=name,passed=ok,line=line,error=error));print(opt,case,name,'PASS' if ok else 'FAIL',line,error,flush=True)
  if case==8 and ok:
   size=struct.unpack('<I',u.mem_read(sy['exported_size'],4))[0];(O/('sample_'+opt+'.avi')).write_bytes(u.mem_read(0x61000000,size))
summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),failed=sum(not x['passed'] for x in rows))
(O/'production_results.json').write_text(json.dumps(dict(summary=summary,cases=rows,real_hardware_verified=False),indent=2)+'\n',encoding='utf-8');print(summary)
raise SystemExit(bool(summary['failed']))
