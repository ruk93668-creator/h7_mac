"""Scope and assertion integrity audit; no exception for failed historical trace comparisons."""
from pathlib import Path
import subprocess,json,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
G=['git','-c','safe.directory='+ROOT.as_posix()];BASE='fa9ce5d'
def git(*args):return subprocess.check_output(G+list(args),cwd=ROOT)
def original(path):return git('show',BASE+':'+path)
def expressions(text,macro):
 result=[];start=0
 while True:
  start=text.find(macro+'(',start)
  if start<0:return result
  a=start+len(macro)+1;i=a;depth=1
  while depth:
   if text[i]=='(':depth+=1
   if text[i]==')':depth-=1
   i+=1
  result.append(''.join(text[a:i-1].split()));start=i
allowed={'CM7/Application/Tasks/camera_task.c','CM7/Application/app_camera.c','CM7/Application/app_camera.h','CM7/Application/app_camera_qualification.c','CM7/Application/app_camera_qualification.h','CM7/Media/Inc/camera_capture.h','CM7/Media/Src/camera_capture.c'}
changed=git('diff','--name-only',BASE).decode().splitlines()
production=[p for p in changed if p.startswith(('CM7/','CM4/','Common/','Drivers/','Middlewares/','MDK-ARM/'))]
assert set(production)==allowed,production
protected=[]
for folder in ['Tests/CameraLifecycleC1','Tests/CameraLifecycleC1_1','Tests/CameraLifecycleC2_1_1']:
 for rel in git('ls-tree','-r','--name-only',BASE,folder).decode().splitlines():
  if rel.endswith(('.c','.h','.py')):
   assert (ROOT/rel).read_bytes()==original(rel),rel
   protected.append(rel)
for rel in ['CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle.h','CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_lifecycle_replay.h','CM7/Application/app_messages.c','CM7/Application/app_messages.h']:
 assert (ROOT/rel).read_bytes()==original(rel),rel
 protected.append(rel)
assertions=[]
for rel,macro in [('Tests/ConsumerQualificationC2_2A/qualification_test.c','CK'),('Tests/ConsumerQualificationC2_2A/adapter_test.c','VERIFY')]:
 old=original(rel).decode('utf-8');new=(ROOT/rel).read_text(encoding='utf-8')
 # Only substitute the explicit asynchronous fixture round for the previous synchronous adapter call.
 new=new.replace('ProcessRound(&proven)','App_QualificationProcess(&s_camera.qualifications,&proven,NULL)')
 a,b=expressions(old,macro),expressions(new,macro)
 assert a==b,rel
 assertions.append({'file':rel,'assertions_preserved':len(a)})
rel='Tests/CameraModuleB1/behavior_test.c';old=original(rel).decode('utf-8').replace('\r\n','\n');new=(ROOT/rel).read_text(encoding='utf-8')
a=old.index('int B1_TestRun(');b=new.index('int B1_TestRun(')
assert old[a:]==new[b:],'B1 scenarios or assertions changed'
main=git('rev-parse','main').decode().strip();assert main=='182c84e7334f0a62d96f1d3109b0d7fb8efb8158',main
report={'baseline':git('rev-parse',BASE).decode().strip(),'branch':git('branch','--show-current').decode().strip(),'main':main,'production_files':production,'protected_sources_and_test_scripts':protected,'assertions':assertions,'b1_test_bodies_unchanged':True,'project_unchanged':(ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()==original('MDK-ARM/h7_mac.uvprojx'),'production_sha256':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sorted(allowed)},'output_untracked':git('ls-files','output').decode()==''}
assert report['project_unchanged'] and report['output_untracked']
(HERE/'scope_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ['protected_sources_and_test_scripts','production_sha256']},indent=2))
