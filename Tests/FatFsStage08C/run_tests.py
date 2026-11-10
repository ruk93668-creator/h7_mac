"""ARMCC5 executes the real FatFS/Platform against isolated RAM FAT images; never opens a real disk."""
from pathlib import Path
import subprocess,json,sys,struct,hashlib,time
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_PC
OUT=ROOT/'output/FatFsStage08C';OUT.mkdir(parents=True,exist_ok=True)
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');commands=[];rows=[]
names=['unprepared','no_card','invalid_signature','protective_gpt','exfat_reject','partition_overflow','readonly_mount','large_roundtrip','task_ownership','pool_and_generation','short_read_eof','disk_full_partial_write','read_timeout_mount','write_timeout','sync_failure','close_failure','remove_cached_file','raw_vs_fatfs','isr_mount','masked_context','recovery_failure_retry','sector_boundaries','partition_write_guard','mbr_mount','partition_overlap','bpb_outside_partition','remount_stale_handle','generation_exhaustion','reentrant_diagnostics','invalid_fat','path_checks','file_size_overflow','existing_and_locked','unmount_open_busy','unsupported_partition','invalid_partition_arg','read_error_recovery','init_timeout_retry','isr_error_isolation','active_device','fat32_roundtrip','fat12_roundtrip','append_roundtrip','accept_readonly','accept_no_write_authority','accept_roundtrip','accept_no_overwrite','accept_wrong_cid','accept_recover_retry','accept_bad_file_index','accept_invalid_command','no_stale_volume_diagnostics','seek_full_short','null_and_empty_io']

names+=['input_chunks','input_copy_failure','input_partial_progress','input_two_spans','input_reentrant_busy','input_independent_aligned']

names+=['write_geometry','write_histogram','write_rejected_not_counted','write_failure_count','write_unmount_preserved','write_remount_reset','write_saturation_io','write_real_cluster_split']

names+=['explicit_partition3']
names+=['io_timing_nested','io_timing_input_failure','io_timing_write_failure','io_timing_tick_wrap','io_timing_saturation','io_timing_remount']

def run(cmd):
    p=subprocess.run(list(map(str,cmd)),capture_output=True);text=(p.stdout+p.stderr).decode(errors='replace');commands.append(dict(command=subprocess.list2cmdline(list(map(str,cmd))),exit_code=p.returncode,output=text));(HERE/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    if p.returncode or 'warning:' in text.lower():raise RuntimeError(text)

def image(kind):
    # Synthetic BPB/FAT fixtures, no mkfs calls or access to host disk devices.
    is32=kind=='fat32';is12=kind=='fat12';offset=1024 if kind=='mbr' else 0
    sectors=131072 if is32 else 128 if is12 else 8192 if offset else 16384
    reserved=32 if is32 else 1;fat=1024 if is32 else 1 if is12 else 32 if offset else 64;roots=0 if is32 else 32 if is12 else 512
    b=bytearray(512);b[:3]=b'\xEB\x3C\x90';b[3:11]=b'MSDOS5.0';struct.pack_into('<HBHBHHBHHHII',b,11,512,1,reserved,2,roots,sectors if sectors<65536 else 0,0xF8,0 if is32 else fat,63,255,offset,sectors if sectors>=65536 else 0)
    if is32:struct.pack_into('<IHHIHH',b,36,fat,0,0,2,1,6);b[64]=0x80;b[66]=0x29;struct.pack_into('<I',b,67,0x12345678);b[71:82]=b'FAT32 TEST ';b[82:90]=b'FAT32   '
    else:b[36]=0x80;b[38]=0x29;struct.pack_into('<I',b,39,0x12345678);b[43:54]=b'FATFS TEST ';b[54:62]=b'FAT12   ' if is12 else b'FAT16   '
    b[510:512]=b'\x55\xAA';f=bytearray(512)
    if is32:struct.pack_into('<III',f,0,0x0ffffff8,0xffffffff,0x0fffffff)
    elif is12:f[:3]=b'\xF8\xFF\xFF'
    else:f[:4]=b'\xF8\xFF\xFF\xFF'
    chunks=[(offset*512,bytes(b)),((offset+reserved)*512,bytes(f)),((offset+reserved+fat)*512,bytes(f))]
    if offset:
        m=bytearray(512);m[450]=0x0C;struct.pack_into('<II',m,454,offset,sectors);m[466]=0x83;struct.pack_into('<II',m,470,10000,1000);m[510:]=b'\x55\xAA';chunks.append((0,bytes(m)))
    return chunks

base=[cc,'--cpu','Cortex-M7','--c99','--split_sections']
for inc in [HERE/'stubs',ROOT/'CM7/Platform',ROOT/'CM7/BSP',ROOT/'Common/Inc',ROOT/'Middlewares/Third_Party/FatFs']:base+=['-I',inc]
for opt in ['O0','O2']:
    obj=OUT/('test_'+opt+'.o');ff=OUT/('ff_'+opt+'.o');axf=OUT/('test_'+opt+'.axf')
    run(base+['-'+opt,'-c',HERE/'fs_test.c','-o',obj]);run(base+['-'+opt,'-c',ROOT/'Middlewares/Third_Party/FatFs/ff.c','-o',ff]);run([cc.with_name('armlink.exe'),obj,ff,'--entry','FatFs_TestRun','--scatter',HERE/'test.sct','-o',axf])
    with axf.open('rb') as f:
        e=ELFFile(f);symbols={v.name:v['st_value'] for v in e.get_section_by_name('.symtab').iter_symbols()};entry=e.header['e_entry'];segments=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
    for case,name in enumerate(names):
        u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
        for addr,size in [(0x08000000,0x100000),(0x20000000,0x20000),(0x60000000,0x4000000 if case==40 else 0x800000),(0x68000000,0x100000),(0x01000000,0x1000)]:u.mem_map(addr,size)
        for addr,data in segments:u.mem_write(addr,data)
        kind='fat32' if case==40 else 'fat12' if case in [11,41,52] else 'mbr' if case in [5,22,23,24,25,34,68] else 'fat16'
        for offset,data in image(kind):u.mem_write(0x60000000+offset,data)
        u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
        seen=[]
        def write_boundary(uc,address,size,data):
            seen.append((uc.reg_read(UC_ARM_REG_R1),uc.reg_read(UC_ARM_REG_R2)))
        u.hook_add(UC_HOOK_CODE,write_boundary,begin=symbols['f_write']&~1,end=symbols['f_write']&~1)
        error=None
        try:u.emu_start(entry|1,0x01000000,count=80000000)
        except Exception as exc:error=str(exc)
        line=u.reg_read(UC_ARM_REG_R0);ok=not error and line==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
        if any(a!=symbols['s_write_input'] or n>8192 for a,n in seen):ok=False;error='FatFS saw external source or oversized input'
        row=dict(f_write_calls=len(seen),optimization=opt,id=case,name=name,result='PASS' if ok else 'FAIL',line=line,error=error);rows.append(row);print(opt,case,row['result'],line,error,flush=True)
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows))
(HERE/'results.json').write_text(json.dumps(dict(summary=summary,cases=rows,real_card_verified=False),indent=2)+'\n');print(summary);raise SystemExit(bool(summary['failed']))
