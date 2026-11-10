from pathlib import Path
import subprocess,os,sys
r=Path(__file__).resolve().parents[2];out=Path(os.environ['TEMP'])/'h7_stageA_48afa27';dest=out/'compat';ref='3083561';dirs=['Tests/FrameLeaseSeven/','Tests/JpegStorageStage08D/','Tests/FatFsStage08C/','Tests/SdmmcStage08B/','Tests/SdmmcStage08A3/']
git=['git','-c','safe.directory='+str(r)]
for folder in ['CM7','CM4','Common','Drivers','Middlewares']:
 q=dest/folder
 if not q.exists():
  dest.mkdir(parents=True,exist_ok=True)
  command="New-Item -ItemType Junction -Path '"+str(q).replace("'","''")+"' -Target '"+str(r/folder).replace("'","''")+"' | Out-Null"
  subprocess.run(['powershell','-NoProfile','-Command',command],check=True)
files=subprocess.check_output(git+['ls-tree','-r','--name-only',ref,'Tests'],cwd=r).decode().splitlines()
for f in files:
 if any(f.startswith(d) for d in dirs) and Path(f).suffix in ['.c','.h','.py','.sct','.jpg']:
  q=dest/f;q.parent.mkdir(parents=True,exist_ok=True);q.write_bytes(subprocess.check_output(git+['show',ref+':'+f],cwd=r))
# Only adapt the new platform link boundary; retain all functional checks.
p=dest/'Tests/FrameLeaseSeven/lease_production_test.c';s=p.read_text(encoding='utf-8');s=s.replace('#include "../../CM7/Media/Src/camera_mjpeg_fifo.c"','#include "../../CM7/Platform/platform_sync.c"\n#include "../../CM7/Media/Src/camera_mjpeg_fifo.c"',1);p.write_text(s,encoding='utf-8')
p=dest/'Tests/JpegStorageStage08D/stubs/stm32h7xx.h';s=p.read_text(encoding='utf-8');s=s.replace('#include <stdint.h>','#include <stdint.h>\ntypedef int IRQn_Type;\nstatic uint32_t NVIC_GetPriority(IRQn_Type irq) { (void)irq;return 7U; }');p.write_text(s,encoding='utf-8')
# Execute real FATFS and lease functional suites against current source junctions.
env=dict(os.environ,PYTHONPATH=str(out/'deps'),PYTHONIOENCODING='utf-8')
failures=[]
for f in ['Tests/FrameLeaseSeven/run_production_tests.py','Tests/FatFsStage08C/run_production_tests.py']:
 p=subprocess.run([sys.executable,str(dest/f)],cwd=dest,env=env,capture_output=True);log=(p.stdout+p.stderr).decode('utf-8',errors='replace');(out/(Path(f).parent.name+'_effective.log')).write_text(log,encoding='utf-8');print(f,p.returncode,log[-1800:],flush=True);failures.append(p.returncode)

# BSP/HAL tests remaining applicable to production, with removed historical pressure/application suites excluded explicitly.
p=dest/'Tests/SdmmcStage08B/run_tests.py';s=p.read_text(encoding='utf-8').replace('for profile,entry_name,case_names,defines in profiles:',"for profile,entry_name,case_names,defines in [p for p in profiles if p[0]=='idma']:")
p=p.with_name('run_stageA_idma.py');p.write_text(s,encoding='utf-8')
r=subprocess.run([sys.executable,str(p)],cwd=dest,env=env,capture_output=True);log=(r.stdout+r.stderr).decode('utf-8',errors='replace');(out/'idma_effective.log').write_text(log,encoding='utf-8');print('IDMA',r.returncode,log[-800:],flush=True)

failures.append(r.returncode)
raise SystemExit(any(failures))
