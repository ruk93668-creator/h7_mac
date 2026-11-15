from pathlib import Path
import subprocess,os,sys,tarfile,io,time,json,shutil
H=Path(__file__).resolve().parent;ROOT=H.parents[2];g=['git','-c','safe.directory='+ROOT.as_posix()]
OUT=ROOT/'output/ConsumerJoint'/('B2B3_ID17_clean_'+str(time.time_ns()));OUT.mkdir(parents=True)
with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(g+['archive','177c0e1'],cwd=ROOT))) as t:t.extractall(OUT,filter='data')
env=dict(os.environ,PYTHONIOENCODING='utf-8',PYTHONPATH=str(ROOT/'Tests/OledAsyncAcceptance/python_deps'),GIT_DIR=subprocess.check_output(g+['rev-parse','--absolute-git-dir'],cwd=ROOT).decode().strip(),GIT_WORK_TREE=str(OUT))
r=subprocess.run([sys.executable,str(OUT/'Tests/ConsumerJointC2_2B/ID17/trace_id17.py')],cwd=OUT,env=env,capture_output=True)
D=H/'RawResults/ID17Clean';D.mkdir(parents=True,exist_ok=True);(D/'execution.log').write_bytes(r.stdout+r.stderr)
src=OUT/'Tests/ConsumerJointC2_2B/ID17';shutil.copy2(src/'results.json',D/'results.json');shutil.copytree(src/'ProbeEvidence',D/'ProbeEvidence',dirs_exist_ok=True)
(H/'rechecks.json').write_text(json.dumps({'initial':'ID17 preflight rejected a snapshot already modified by prior result writers; original log preserved','fix':'Run ID17 first in a clean archive; no source/predicate changes','snapshot':str(OUT),'exit':r.returncode},indent=2));print('clean ID17 exit',r.returncode);raise SystemExit(r.returncode)
