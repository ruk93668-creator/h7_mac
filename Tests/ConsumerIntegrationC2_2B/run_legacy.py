"""Run original assertions unchanged; missing bus dependency is explicit test-only fixture."""
from pathlib import Path
import os,sys,runpy,subprocess,time,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
script=sys.argv[1]
if (ROOT/'.git').exists():raise SystemExit('Run legacy assertions in an isolated source snapshot; preserve original reports.')
OUT=ROOT/'output/ConsumerIntegrationC2_2B'/('legacy_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
obj=OUT/'transport.o';c.run(c.cc+['-c',HERE/'legacy_transport_fixture.c','-o',obj])
extra=[]
for f in ['app_camera_consumer.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']:
 target=OUT/(f+'.o');c.run(c.cc+['-c',ROOT/'CM7/Application'/f,'-o',target]);extra.append(str(target))
original=subprocess.run
# Only ARMLINK receives missing transport symbols; neither tests nor expected traces are edited.
def run(cmd,*args,**kwargs):
 if isinstance(cmd,(list,tuple)) and str(cmd[0]).endswith('armlink.exe'):
  cmd=list(cmd)+[str(obj)]
  entry=str(cmd[cmd.index('--entry')+1])
  if entry.startswith(('B1_','B2_','B3_')) or entry=='R2_StopRun':cmd+=extra
 return original(cmd,*args,**kwargs)
subprocess.run=run
if script.startswith('Tests/CameraModuleB1/'):
 sys.path.insert(0,str(ROOT/'Tests/CameraModuleB1'))
 sys.modules.pop('run_tests',None)
sys.argv=[str(ROOT/script)]
runpy.run_path(str(ROOT/script),run_name='__main__')
