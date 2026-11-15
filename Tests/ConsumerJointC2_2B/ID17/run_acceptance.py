"""Run requested commands in an immutable source snapshot; preserve original reports."""
from pathlib import Path
import os,sys,subprocess,json,tarfile,io,time,shutil,hashlib
H=Path(__file__).resolve().parent;ROOT=H.parents[2];BASE='8a83591b4baa0a850ccdfb269b68d752af5bee81'
g=['git','-c','safe.directory='+ROOT.as_posix()]
assert not subprocess.check_output(g+['diff',BASE,'--','CM7','CM4','Common','Board','MDK-ARM'],cwd=ROOT)
OUT=ROOT/'output/ConsumerJoint'/('ID17_'+str(time.time_ns()));OUT.mkdir(parents=True)
with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(g+['archive',BASE],cwd=ROOT))) as t:t.extractall(OUT,filter='data')
env=dict(os.environ,PYTHONIOENCODING='utf-8',PYTHONPATH=str(ROOT/'Tests/OledAsyncAcceptance/python_deps'),GIT_DIR=subprocess.check_output(g+['rev-parse','--absolute-git-dir'],cwd=ROOT).decode().strip(),GIT_WORK_TREE=str(OUT))
items=[
 ('consumer','Tests/ConsumerIntegrationC2_2B/run_tests.py',['Tests/ConsumerIntegrationC2_2B/results.json'],1,{}),
 ('business','Tests/ConsumerIntegrationC2_2B/run_business.py',['Tests/ConsumerIntegrationC2_2B/business_results.json'],0,{}),
 ('joint','Tests/ConsumerJointC2_2B/run_tests.py',['Tests/ConsumerJointC2_2B/results.json'],0,{}),
 ('contracts','Tests/ConsumerJointC2_2B/check_contracts.py',['Tests/ConsumerJointC2_2B/contracts.json'],0,{}),
 ('exit','Tests/ConsumerJointC2_2B/run_exit.py',['Tests/ConsumerJointC2_2B/exit_results.json'],0,{}),
 ('id7','Tests/ConsumerJointC2_2B/run_id7.py',['Tests/ConsumerJointC2_2B/ID7/results.json'],0,{}),
 ('id7_joint','Tests/ConsumerJointC2_2B/run_id7_joint.py',['Tests/ConsumerJointC2_2B/ID7/joint/results.json'],0,{}),
 ('b2_O0','Tests/ConsumerJointC2_2B/run_id7_b2.py',['Tests/ConsumerJointC2_2B/ID7/legacy/O0/usb_compat.json'],0,{'ID7_OPT':'O0'}),
 ('b2_O2','Tests/ConsumerJointC2_2B/run_id7_b2.py',['Tests/ConsumerJointC2_2B/ID7/legacy/O2/usb_compat.json'],0,{'ID7_OPT':'O2'}),
 ('history','Tests/ConsumerJointC2_2B/run_id7_regression.py',['Tests/ConsumerJointC2_2B/ID7/regression_runs.json'],0,{}),
 ('build','Tests/ConsumerJointC2_2B/run_id7_build.py',['Tests/ConsumerJointC2_2B/ID7/build.json'],0,{}),
 ('resources','Tests/ConsumerJointC2_2B/audit_id7_resources.py',['Tests/ConsumerJointC2_2B/ID7/abi_map_stack.json','Tests/ConsumerJointC2_2B/ID7/resource_delta.json'],0,{}),
 ('id7_contracts','Tests/ConsumerJointC2_2B/check_id7.py',['Tests/ConsumerJointC2_2B/ID7/contracts.json'],0,{})]
D=H/'Evidence';D.mkdir(exist_ok=True);rows=[]
for name,script,reports,expected,extra in items:
 started=time.time();e=dict(env,**extra);e.pop('ID7_OPT',None);e.update(extra)
 p=subprocess.run([sys.executable,str(OUT/script)],cwd=OUT,env=e,capture_output=True)
 (D/(name+'.log')).write_bytes(p.stdout+p.stderr)
 copied=[]
 for rel in reports:
  f=OUT/rel
  if f.exists():
   target=D/(name+'_'+f.name);shutil.copy2(f,target);copied.append(str(target.relative_to(H)))
 row={'name':name,'command':[sys.executable,script],'environment':extra,'exit':p.returncode,'expected_exit':expected,'exit_as_expected':p.returncode==expected,'seconds':round(time.time()-started,2),'reports':copied,'log':str((D/(name+'.log')).relative_to(H))}
 rows.append(row);print(row,flush=True)
 (H/'execution.json').write_text(json.dumps({'baseline':BASE,'snapshot':str(OUT),'runs':rows},indent=2))
# Preserve all fresh legacy reports/logs produced by the requested nested regression runner.
src=OUT/'Tests/ConsumerJointC2_2B/ID7/Evidence/regression';dst=D/'history';shutil.copytree(src,dst,dirs_exist_ok=True)
shutil.copytree(OUT/'Tests/ConsumerJointC2_2B/ID7/Evidence/build',D/'build',dirs_exist_ok=True)
# Archive this run's events separately from every earlier evidence directory.
for source,target in [
 (OUT/'Tests/ConsumerJointC2_2B/Evidence',D/'joint_events'),
 (OUT/'Tests/ConsumerJointC2_2B/ID7/Evidence',D/'id7_events'),
 (OUT/'Tests/ConsumerJointC2_2B/ID7/joint/Evidence',D/'id7_joint_events')]:
 target.mkdir(exist_ok=True)
 for f in source.glob('*.json.gz'):shutil.copy2(f,target/f.name)
# Keep the unmodified negative control and unchanged BLOCKED snapshot hash.
unchanged={}
for rel in ['Tests/ConsumerJointC2_2B/ID7/baseline_results.json','Tests/ConsumerJointC2_2B/legacy_153_gate.json']:
 assert (ROOT/rel).read_bytes()==(OUT/rel).read_bytes();unchanged[rel]=hashlib.sha256((ROOT/rel).read_bytes()).hexdigest()
manifest={'baseline':BASE,'snapshot':str(OUT),'historical_unchanged':unchanged,'production_files_modified':[],'required_command_count':len(rows),'exit_mismatches':[r['name'] for r in rows if not r['exit_as_expected']]}
(H/'manifest.json').write_text(json.dumps(manifest,indent=2))
raise SystemExit(bool(manifest['exit_mismatches']))
