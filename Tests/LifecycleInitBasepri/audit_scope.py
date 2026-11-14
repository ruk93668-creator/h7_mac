"""Verify the only production delta is the approved pre-scheduler guard."""
from pathlib import Path
import subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
BASE='2a927374c5cbdaa5e6bca8267e0ccf500d041045'
G=['git','-c','safe.directory='+ROOT.as_posix()]
def git(*args):return subprocess.check_output(G+list(args),cwd=ROOT)
changed=git('diff','--name-only',BASE).decode().splitlines()
production=[x for x in changed if x.startswith(('CM7/','CM4/','Common/','Drivers/','Middlewares/','MDK-ARM/'))]
assert production==['CM7/Application/app_messages.c'],production
path='CM7/Application/app_messages.c'
old=git('show',BASE+':'+path).decode('utf-8').replace('\r\n','\n')
new=(ROOT/path).read_text(encoding='utf-8')
a=old.index('SysResult_t App_LifecycleMessagesInit(void)');z=old.index('\n}',a)+2
b=new.index('SysResult_t App_LifecycleMessagesInit(void)');w=new.index('\n}',b)+2
assert old[:a]==new[:b] and old[z:]==new[w:],'Code outside init changed'
clean=lambda s:'\n'.join(x for x in s.splitlines() if not x.lstrip().startswith('//'))
assert clean(old[a:z]).replace('Platform_SyncGetPrimask() != 0U || Platform_SyncGetBasepri() != 0U ||','Platform_SyncGetPrimask() != 0U ||')==clean(new[b:w])
protected=[]
for folder in ['Tests/CameraLifecycleC1','Tests/CameraLifecycleC1_1','Tests/CameraLifecycleC2_1_1']:
 for rel in git('ls-tree','-r','--name-only',BASE,folder).decode().splitlines():
  if rel.endswith(('.py','.c','.h')):
   assert (ROOT/rel).read_bytes()==git('show',BASE+':'+rel),rel
   protected.append(rel)
for rel in ['CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h','MDK-ARM/h7_mac.uvprojx']:
 assert (ROOT/rel).read_bytes()==git('show',BASE+':'+rel),rel
 protected.append(rel)
report={'baseline':BASE,'branch':git('branch','--show-current').decode().strip(),'production_delta':production,'outside_init_identical':True,'guard_removal_only':True,'protected_files':protected,'existing_assertions_unchanged':True,'no_basepri_clear_added':True,'main':git('rev-parse','main').decode().strip(),'r2_branch':git('rev-parse','codex/stage-C2-2A-R2-stop-ownership').decode().strip(),'production_sha256':hashlib.sha256((ROOT/path).read_bytes()).hexdigest()}
assert report['r2_branch']==BASE
(HERE/'scope_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='protected_files'},indent=2))
