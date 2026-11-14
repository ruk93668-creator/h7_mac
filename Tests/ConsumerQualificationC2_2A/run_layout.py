"""Actual ARMCC5 ABI plus Map and static callgraph, not hardware stack validation."""
from pathlib import Path
import os,sys,subprocess,json,re,html,struct,inspect
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/ConsumerQualificationC2_2A/layout';OUT.mkdir(parents=True,exist_ok=True)
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
baseline=OUT/'baseline_app_camera.c'
baseline.write_bytes(subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'show','c6d2bed:CM7/Application/app_camera.c'],cwd=ROOT))
scope=dict(c.__dict__)
exec(inspect.getsource(c.execute).replace('return result',"result['abi_words']=list(struct.unpack('<12I',u.mem_read(syms['g_layout'],48))); return result"),scope)
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 for name,source in [('baseline',baseline),('current',ROOT/'CM7/Application/app_camera.c')]:
  image=name+'_'+opt
  defs=['-DB1_APP_SOURCE="'+source.as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']
  c.build(image,[HERE/'layout_test.c'],'C2A_LayoutRun',defs)
  r=scope['execute'](image,'C2A_LayoutRun',0);assert r['pass'],r
  assert r['abi_words'][2:12]==[824,72,32,56,52,24,6144,3,20,144],r['abi_words']
  # ARMCC emitted literal structure sizes in the ABI probe; Map also retains exact s_camera allocation.
  mp=(OUT/(image+'.map')).read_text(encoding='utf-8',errors='replace')
  m=re.search(r'\bs_camera\s+(0x[\da-fA-F]+)\s+Data\s+(\d+)',mp)
  graph=(OUT/(image+'.htm')).read_text(encoding='utf-8',errors='replace')
  functions={}
  for fn in ['CameraTask_Process','App_CameraHandleCommand','App_CameraStartResource','App_CameraFinishRound']:
   match=re.search(r'<P><STRONG><a name="[^\"]+"></a>'+fn+r'</STRONG>(.*?)(?=<P><STRONG>|$)',graph,re.S)
   text=html.unescape(re.sub('<[^>]+>',' ',match[1])) if match else ''
   stack=re.search(r'Stack size (\d+) bytes',text);depth=re.search(r'Max Depth = (\d+)',text)
   functions[fn]={'local':int(stack[1]) if stack else None,'known_depth':int(depth[1]) if depth else None}
  rows.append({'variant':name,'optimization':opt,'abi_words':r['abi_words'],'context_address':m[1] if m else None,'context_bytes':int(m[2]) if m else None,'stack':functions,'unit_assertions_pass':r['pass']})
assert all(r['context_bytes'] is not None for r in rows)
(HERE/'layout_results.json').write_text(json.dumps({'rows':rows,'core_bytes':824,'rx_pair_bytes':664,'qualification_semantics_bytes':160,'decision_bytes':32,'task_stack_bytes':6144,'hardware_stack_verified':False},indent=2),encoding='utf-8')
print(json.dumps(rows,indent=2))
