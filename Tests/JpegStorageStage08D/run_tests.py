"""Execute real Stage08D C sources with ARMCC5/Unicorn and RAM-only mock files."""
from pathlib import Path
import subprocess,json,sys,struct,hashlib,shutil
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
R=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent;O=R/'output/Stage08D';O.mkdir(parents=True,exist_ok=True)
commands=[];rows=[]
def command(args):
 p=subprocess.run(list(map(str,args)),capture_output=True,cwd=R)
 record=dict(command=subprocess.list2cmdline(list(map(str,args))),exit_code=p.returncode,output=(p.stdout+p.stderr).decode(errors='replace'));commands.append(record)
 (H/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
 if p.returncode:raise RuntimeError(record['output'])
 return record
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');command([cc,'--vsn'])
names=['jpeg_valid','jpeg_split_soi','jpeg_truncated','jpeg_bad_spans','jpeg_wrong_dimensions','reject_yuy2','idle_no_card_io','snapshot_roundtrip','snapshot_split','pool_full','sampling_pace','oversize','command_full','reject_isr','reject_nonjpeg','no_card','disk_full','write_timeout','short_write','sync_failure','read_corruption','filename_search_bound','release_ownership','stop_drains_four','stop_without_frames','video_snapshot_exclusion','recover_failure_retry','stale_release','session_exhaustion','bad_config','network_ai_unsupported','frame_identity','wrong_owner','bad_metadata','unmount_failure','status_no_io','avi_three_frames','avi_segments','record_limit','tick_wrap','index_limit','avi_budget','avi_empty','avi_partial','avi_sync_failure','avi_index_failure','avi_index_padding','null_span','fault_discards_queue','new_session','stop_during_copy','capture_timeout','bad_jpeg_recycled','snapshot_while_video','test_mode_rejected','status_snapshot','space_graceful_finish','avi_finalized_guard','limit_drains_without_empty_segment','frame_id_exhaustion','sd_only_real_dispatch','usb_sd_shared_lease','sd_fault_keeps_usb_loan','usb_abort_failure_sd_stop','usb_only_no_sd_io','full_pool_usb_continues','usb_sd_discontiguous_spans','source_reuse_after_media_return','maximum_70000_byte_jpeg','card_removed_before_write','recover_no_card_ready_fs','recover_filename_conflict_ready_fs','recover_mounted_without_fs_fault','queue_generation_rejected','queue_session_rejected']
for opt in ['O0','O2']:
 obj=O/('test_'+opt+'.o');axf=O/('test_'+opt+'.axf')
 args=[cc,'--cpu','Cortex-M7','--c99','-'+opt,'--split_sections']
 for d in [H/'stubs',R/'CM7/Application',R/'CM7/Service',R/'CM7/Platform',R/'CM7/Media/Inc',R/'CM7/BSP',R/'Common/Inc',R/'CM7/Framework',R/'Middlewares/ST/STM32_USB_Device_Library/Core/Inc']:args+=['-I',d]
 command(args+['-c',H/'storage_test.c','-o',obj]);command([cc.with_name('armlink.exe'),obj,'--entry','Stage08D_TestRun','--scatter',H/'test.sct','-o',axf])
 with axf.open('rb') as f:
  e=ELFFile(f);entry=e.header['e_entry'];segments=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']];sy={s.name:s['st_value'] for s in e.get_section_by_name('.symtab').iter_symbols()}
 for case,name in enumerate(names):
  u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for a,size in [(0x08000000,0x100000),(0x20000000,0x20000),(0x60000000,0x100000),(0x61000000,0x1000000),(0xD0000000,0x400000),(0x01000000,0x1000)]:u.mem_map(a,size)
  for a,data in segments:u.mem_write(a,data)
  u.mem_write(0x60000000,(H/'fixtures/sample.jpg').read_bytes());u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
  error=None
  try:u.emu_start(entry|1,0x01000000,count=100000000)
  except Exception as x:error=str(x)+' PC='+hex(u.reg_read(UC_ARM_REG_PC))
  line=u.reg_read(UC_ARM_REG_R0);ok=error is None and line==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
  rows.append(dict(optimization=opt,id=case,name=name,passed=ok,line=line,error=error));print(opt,case,name,'PASS' if ok else 'FAIL',line,error,flush=True)
  if case==36 and ok:
   size=struct.unpack('<I',u.mem_read(sy['exported_size'],4))[0];(O/('sample_'+opt+'.avi')).write_bytes(u.mem_read(0x61000000,size))
summary=dict(total=len(rows),passed=sum(x['passed'] for x in rows),failed=sum(not x['passed'] for x in rows))
(H/'results.json').write_text(json.dumps(dict(summary=summary,cases=rows,real_hardware_verified=False),indent=2)+'\n',encoding='utf-8');print(summary)
raise SystemExit(bool(summary['failed']))
