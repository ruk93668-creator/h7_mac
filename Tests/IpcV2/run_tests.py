"""Compile real ARMCC IPC sources and run deterministic interleavings in Unicorn."""
from pathlib import Path
import subprocess,sys,json,struct
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/IpcV2';OUT.mkdir(parents=True,exist_ok=True)
sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
commands=[]
def run(args):
 args=list(map(str,args));r=subprocess.run(args,cwd=ROOT,capture_output=True)
 text=(r.stdout+r.stderr).decode(errors='replace')
 commands.append(dict(command=subprocess.list2cmdline(args),exit_code=r.returncode,output=text))
 (HERE/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 if r.returncode:raise RuntimeError(text)
 if 'warning:' in text.lower():print(text,flush=True)
 return text
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe');link=cc.with_name('armlink.exe')
version=run([cc,'--vsn']).strip();rows=[]
cases=list(range(64))+list(range(100,132))+list(range(200,232))
for role in ['CM7','CM4']:
 for optimization in ['O0','O2']:
  folder=OUT/(role+'_'+optimization);folder.mkdir(exist_ok=True)
  flags=[cc,'--cpu','Cortex-M7' if role=='CM7' else 'Cortex-M4','--c99','-'+optimization,'-DCORE_'+role,'-DIPC_CORE_TEST']
  for inc in ['Tests/IpcV2/stubs','Common/Inc',role+'/Platform',role+'/Service']:
   flags+=['-I',ROOT/inc]
  objects=[]
  for src in ['Tests/IpcV2/protocol_test.c','Common/Src/ipc_core.c','Common/Src/ipc_shared.c','Common/Src/ipc_runtime.c','Common/Src/ipc_application.c']:
   obj=folder/(Path(src).stem+'.o');run(flags+['-c',ROOT/src,'-o',obj]);objects.append(obj)
  axf=folder/'ipc_test.axf'
  run([link,*objects,'--entry','Ipc_TestRun','--scatter',HERE/'test.sct','--no_scanlib','--map','--list',folder/'ipc_test.map','-o',axf])
  with axf.open('rb') as f:
   elf=ELFFile(f);entry=elf.header['e_entry']
   sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
   symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
  assert symbols['g_ipc']==0x38000000
  for case in cases:
   u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
   for address,size in [(0x08000000,0x100000),(0x20000000,0x20000),(0x38000000,0x1000),(0x01000000,0x1000)]:u.mem_map(address,size)
   for address,data in sections:u.mem_write(address,data)
   u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
   error=None
   try:u.emu_start(entry|1,0x01000000,count=5000000)
   except Exception as exc:error=str(exc)
   result=u.reg_read(UC_ARM_REG_R0);passed=error is None and result==0 and u.reg_read(UC_ARM_REG_PC)==0x01000000
   row=dict(role=role,optimization=optimization,case=case,result='PASS' if passed else 'FAIL',failure_line=result,emulator_error=error)
   rows.append(row)
   if not passed:print(row,flush=True)
  print(role,optimization,'PASS',sum(r['result']=='PASS' for r in rows if r['role']==role and r['optimization']==optimization),'/',len(cases),flush=True)
summary=dict(total=len(rows),passed=sum(r['result']=='PASS' for r in rows),failed=sum(r['result']=='FAIL' for r in rows))
(HERE/'results.json').write_text(json.dumps(dict(compiler=version,summary=summary,cases=rows,hardware_verified=False,limitations=['Mock barriers do not validate hardware bus ordering','No reset recovery or periodic heartbeat tested']),ensure_ascii=False,indent=2),encoding='utf-8')
print(summary);raise SystemExit(bool(summary['failed']))
