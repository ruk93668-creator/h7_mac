"""ARMCC5 ABI, current and baseline maps; static stack estimates are not board evidence."""
from pathlib import Path
import os,sys,subprocess,json,re,html,struct,inspect,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraStopOwnershipR2/layout';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
import run_tests as c
original=c.run
def mapped(cmd):
 cmd=list(cmd)
 if str(cmd[0]).endswith('armlink.exe'):
  axf=Path(cmd[cmd.index('-o')+1]);cmd+=['--map','--list',str(axf.with_suffix('.map')),'--callgraph','--symbols']
 return original(cmd)
c.run=mapped
# Extract matching baseline headers: never compile an old Ops initializer against the new Ops layout.
base=OUT/'baseline';base.mkdir(exist_ok=True)
g=['git','-c','safe.directory='+ROOT.as_posix()]
for rel in ['app_camera.c','app_camera.h','app_camera_qualification.h','Tasks/camera_task.c']:
 p=base/rel;p.parent.mkdir(exist_ok=True)
 p.write_bytes(subprocess.check_output(g+['show','fa9ce5d:CM7/Application/'+rel],cwd=ROOT))
scope=dict(c.__dict__)
exec(inspect.getsource(c.execute).replace('return result',"result['abi']=list(struct.unpack('<18I',u.mem_read(syms['g_layout'],72))); return result"),scope)
def stacks(path):
 graph=path.read_text(errors='replace');out={}
 for fn in ['CameraTask_Run','CameraTask_Process','App_CameraHandleCommand','App_CameraStartResource','App_CameraBeginRound','App_CameraAdvanceStop','App_CameraFinishRound','CameraCapture_GetStopFacts']:
  match=re.search(r'<P><STRONG><a name="[^\"]+"></a>'+fn+r'</STRONG>(.*?)(?=<P><STRONG>|$)',graph,re.S)
  text=html.unescape(re.sub('<[^>]+>',' ',match[1])) if match else ''
  stack=re.search(r'Stack size (\d+) bytes',text);depth=re.search(r'Max Depth = (\d+)',text)
  out[fn]={'local':int(stack[1]) if stack else None,'known_depth':int(depth[1]) if depth else None}
 return out
rows=[]
for opt in ['O0','O2']:
 for variant in ['baseline','current']:
  cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
  if variant=='baseline':cc=cc[:1]+['-I',base]+cc[1:]
  old=c.cc;c.cc=cc
  folder=base if variant=='baseline' else ROOT/'CM7/Application'
  defs=['-DB1_APP_SOURCE="'+(folder/'app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(folder/'Tasks/camera_task.c').as_posix()+'"']
  name=variant+'_'+opt;c.build(name,[HERE/'layout_test.c'],'R2_LayoutRun',defs);c.cc=old
  r=scope['execute'](name,'R2_LayoutRun',0);assert r['pass'],r
  expected=[948 if variant=='baseline' else 988,824,72,32,56,52,24,6144,3,20 if variant=='baseline' else 28,144,12,20,0,4,8,12,40]
  assert r['abi']==expected,(r['abi'],expected)
  rows.append({'variant':variant,'optimization':opt,'abi':r['abi'],'stack':stacks(OUT/(name+'.htm'))})
formal=[]
for variant,folder in [('baseline_previous_output',ROOT/'output/CameraStopOwnershipR2/build/previous_output'),('current',ROOT/'MDK-ARM')]:
 for target in ['h7_mac_CM7','h7_mac_CM4']:
  p=folder/target/(target+'.map');s=p.read_text(errors='replace')
  totals=re.search(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+Grand Totals',s,re.M)
  symbols={}
  for name in ['s_camera','s_camera_stack','s_stop_attempt_sequence','s_stop_hardware_result']:
   m=re.search(r'\b'+name+r'\s+(0x[\da-fA-F]+)\s+Data\s+(\d+)',s)
   if m:symbols[name]={'address':m[1],'bytes':int(m[2])}
  formal.append({'variant':variant,'target':target,'map_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'totals':list(map(int,totals.groups())) if totals else None,'symbols':symbols,'stack':stacks(p.with_suffix('.htm')) if p.with_suffix('.htm').exists() else {}})
report={'abi_columns':['camera_context','qualification','entry','decision','lifecycle','old_message','camera_frame','camera_stack','priority','ops','rx_offset','stop_facts','identity_offset','session_offset','request_offset','consumer_offset','generation_offset','debug_size'],'rows':rows,'formal':formal,'hardware_stack_verified':False,'stack_limitations':'Static known-depth only; indirect calls, exception frames and library unknowns require hardware high-water verification.','baseline_artifact_provenance':'Preserved previous formal output; ABI baseline independently compiled from fa9ce5d source/headers.'}
(HERE/'layout_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({'rows':rows,'formal':formal},indent=2))
