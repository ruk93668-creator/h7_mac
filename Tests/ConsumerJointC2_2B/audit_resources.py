from pathlib import Path
import json,hashlib,re,sys,os,struct,subprocess,time
ROOT=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent
# 使用真实ARMCC5生成ABI字段，不将文档常数当成测量结果。
os.environ['C1_TEST_OUTPUT_ROOT']=str(ROOT/'output/ConsumerJoint'/('abi_'+str(time.time_ns())));sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.build('abi',[ROOT/'Tests/ConsumerIntegrationC2_2B/layout_probe.c'],'Layout_Run')
syms,sections=c.images['abi'];address=syms['g_c2b_abi'];data=None
for a,b in sections:
 if a<=address and address+40<=a+len(b):data=b[address-a:address-a+40]
assert data is not None
abi=list(struct.unpack('<10I',data));assert abi==[232,144,332,824,56,52,24,40,144,200]
manifest=json.loads((H/'build.json').read_text());maps=[]
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
(H/'abi_map_stack.json').write_text(json.dumps({'ABI':abi,'columns':['consumer','tx','rx','qualification','lifecycle_message','AppMessage','CameraFrame','CameraDebug','request_offset','generation_offset'],'map':maps,'stack_verified_on_hardware':False,'all_indirect_call_depth_proven':False,'production_tree_unchanged_from_baseline':True,'static_RAM_delta_this_task':0},indent=2))
print('ABI',abi)

g=['git','-c','safe.directory='+ROOT.as_posix()]
base='cc9f25a54d95cdfbe332c1c5f7a4791ff06e68d3'
assert subprocess.check_output(g+['diff',base,'--','CM7','CM4','Board','Common','MDK-ARM'],cwd=ROOT)==b''
previous=json.loads((ROOT/'Tests/ConsumerIntegrationB2B6/abi_map_stack.json').read_text())
for a,b in zip(maps,previous['map']):assert a['totals']==b['totals'] and a['symbols']==b['symbols']
manifest['baseline_commit']=base
manifest['production_directories_unchanged']=True
manifest['new_tests_registered_in_firmware_project']=False
manifest['source_hashes']={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'CM7/Application').rglob('*.c')}
manifest['test_source_hashes']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in H.glob('*') if p.suffix in ['.py','.c','.h']}
(H/'source_manifest.json').write_text(json.dumps(manifest,indent=2))

c.build('trace_layout',[H/'layout_probe.c'],'Joint_Layout')
syms,sections=c.images['trace_layout'];addr=syms['g_joint_offsets']
for start,data in sections:
 if start<=addr and addr+32<=start+len(data):offsets=list(struct.unpack('<8I',data[addr-start:addr-start+32]))
assert offsets==[52,56,1,4,8,12,16,220]
(H/'trace_layout.json').write_text(json.dumps({'ARMCC5_offsets':offsets,'columns':['AppMessage_size','Lifecycle_size','source','command_id','stream_epoch','lease_id','result','consumer_granted'],'enum_packing':'AppMessage enums occupy byte fields on ARMCC5; lifecycle fields fixed uint32'},indent=2))
