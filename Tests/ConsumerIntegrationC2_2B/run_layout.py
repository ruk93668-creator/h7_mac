from pathlib import Path
import os,sys,json,time,inspect,struct,re,html,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/ConsumerIntegrationC2_2B'/('layout_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
scope=dict(c.__dict__);exec(inspect.getsource(c.execute).replace('return result',"result['abi']=list(struct.unpack('<10I',u.mem_read(syms['g_c2b_abi'],40)));return result"),scope)
rows=[]
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc];name='layout_'+opt
 c.build(name,[HERE/'layout_probe.c'],'Layout_Run');r=scope['execute'](name,'Layout_Run',0)
 assert r['pass'] and r['abi']==[232,144,332,824,56,52,24,40,144,200],r
 rows.append({'optimization':opt,'abi':r['abi']})
baseline=json.loads((ROOT/'Tests/CameraStopOwnershipR2/FinalCombination20261114/scope_and_firmware.json').read_text(encoding='utf-8'))
formal=[]
def stack(path):
 text=path.read_text(errors='replace');result={}
 for fn in ['CameraTask_Run','CameraTask_Process','App_CameraFinishRound','UsbTask_Run','UsbTask_Process','StorageTask_Run','App_StorageProcess','ControlTask_Run','App_CameraConsumerProcess']:
  m=re.search(r'<P><STRONG><a name="[^"]+"></a>'+fn+r'</STRONG>(.*?)(?=<P><STRONG>|$)',text,re.S)
  s=html.unescape(re.sub('<[^>]+>',' ',m[1])) if m else ''
  local=re.search(r'Stack size (\d+) bytes',s);depth=re.search(r'Max Depth = (\d+)',s)
  result[fn]={'local':int(local[1]) if local else None,'known_depth':int(depth[1]) if depth else None}
 return result
for target in ['h7_mac_CM7','h7_mac_CM4']:
 old=Path(baseline['firmware'][target]['.map']['path']);assert hashlib.sha256(old.read_bytes()).hexdigest()==baseline['firmware'][target]['.map']['sha256']
 for label,path in [('main_f0ce45f',old),('candidate',ROOT/'MDK-ARM'/target/(target+'.map'))]:
  s=path.read_text(errors='replace');totals=re.search(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+Grand Totals',s,re.M)
  symbols={}
  for m in re.finditer(r'^\s*(s_camera|s_usb|s_camera_consumer|s_camera_stack|s_usb_stack|s_control_stack|s_ipc_stack|s_stack|s_lifecycle_storage|s_lifecycle_queues|g_usb_camera_consumer|g_storage_camera_consumer)\s+(0x[\da-fA-F]+)\s+Data\s+(\d+)\s+([^\r\n]+)',s,re.M):symbols[m[1]]={'address':m[2],'bytes':int(m[3]),'object':m[4]}
  formal.append({'target':target,'variant':label,'totals':list(map(int,totals.groups())) if totals else None,'symbols':symbols,'stack':stack(path.with_suffix('.htm')),'map_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
result={'abi_columns':['consumer','tx','rx','qualification','lifecycle_message','frame_message','frame','camera_debug','request_offset','generation_offset'],'abi':rows,'formal':formal,'hardware_stack_verified':False,'static_depth_limitations':'Indirect callbacks, interrupts and library unknowns prevent complete worst-case stack proof.'}
(HERE/'layout_results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2))
