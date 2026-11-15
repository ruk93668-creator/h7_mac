from pathlib import Path
import json,hashlib,re,sys,os,struct,subprocess,time
ROOT=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent;D=H/'ID7'
# 使用真实ARMCC5生成ABI字段，不将文档常数当成测量结果。
os.environ['C1_TEST_OUTPUT_ROOT']=str(ROOT/'output/ConsumerJoint'/('abi_'+str(time.time_ns())));sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.build('abi',[ROOT/'Tests/ConsumerIntegrationC2_2B/layout_probe.c'],'Layout_Run')
syms,sections=c.images['abi'];address=syms['g_c2b_abi'];data=None
for a,b in sections:
 if a<=address and address+40<=a+len(b):data=b[address-a:address-a+40]
assert data is not None
abi=list(struct.unpack('<10I',data));assert abi==[232,144,332,824,56,52,24,40,144,200]
manifest=json.loads((D/'build.json').read_text());maps=[]
for target in manifest['targets']:
 f=Path(target['artifacts']['map']['path']);s=f.read_text(errors='replace');ht=f.with_suffix('.htm').read_text(errors='replace');totals=re.search(r'^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+Grand Totals',s,re.M);stack={}
 for fn in ['CameraTask_Run','UsbTask_Run','StorageTask_Run','App_CameraConsumerProcess','App_QualificationProcess']:
  m=re.search(r'<P><STRONG><a name="[^"]+"></a>'+fn+r'</STRONG>(.*?)(?=<P><STRONG>|$)',ht,re.S)
  if m:
   text=re.sub('<[^>]+>',' ',m[1]);local=re.search(r'Stack size (\d+) bytes',text);depth=re.search(r'Max Depth = (\d+)',text);stack[fn]={'local':local[1] if local else None,'known_depth':depth[1] if depth else None}
 symbols={}
 for name in ['s_camera_stack','s_usb_stack','s_stack','s_camera_consumer','s_lifecycle_storage']:
  m=re.search(r'^\s*'+name+r'\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)',s,re.M)
  if m:symbols[name]={'address':m[1],'bytes':int(m[2])}
 maps.append({'target':target['target'],'totals':list(map(int,totals.groups())) if totals else None,'symbols':symbols,'stack':stack})
(D/'abi_map_stack.json').write_text(json.dumps({'ABI':abi,'columns':['consumer','tx','rx','qualification','lifecycle_message','AppMessage','CameraFrame','CameraDebug','request_offset','generation_offset'],'map':maps,'stack_verified_on_hardware':False,'all_indirect_call_depth_proven':False,'only_app_usb_changed':True,'static_RAM_delta_this_task':0},indent=2))
print('ABI',abi)


previous=json.loads((H/'abi_map_stack.json').read_text());deltas=[]
for a,b in zip(maps,previous['map']):
 assert a['totals'][3:5]==b['totals'][3:5]
 assert a['symbols']==b['symbols']
 deltas.append({'target':a['target'],'RW_delta':a['totals'][3]-b['totals'][3],'ZI_delta':a['totals'][4]-b['totals'][4],'stack_before':b['stack'],'stack_after':a['stack']})
manifest=json.loads((D/'build.json').read_text());oldmap=json.loads((H/'build.json').read_text());contexts=[]
for build,label in [(oldmap,'baseline'),(manifest,'candidate')]:
 p=Path(build['targets'][0]['artifacts']['map']['path']);text=p.read_text(errors='replace')
 m=re.search(r'^\s*s_usb\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)',text,re.M);assert m
 contexts.append({'version':label,'address':m[1],'bytes':int(m[2])})
assert contexts[0]['bytes']==contexts[1]['bytes']
(D/'resource_delta.json').write_text(json.dumps({'deltas':deltas,'AppUsbContext':contexts,'ABI':abi,'indirect_worst_case_stack_proven':False,'new_state_fields':0},indent=2))
print('RAM/context delta zero; stack configurations unchanged')
