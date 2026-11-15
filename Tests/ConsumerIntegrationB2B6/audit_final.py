from pathlib import Path
import json,hashlib,re,sys,os,struct
ROOT=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent
raw=json.loads((H/'Evidence/original/runs.json').read_text());snap=Path(raw[0]['snapshot']);rows=[]
for group,suite,folder in [('usb','b2','UsbModuleB2'),('storage','b3','StorageModuleB3')]:
 old=json.loads((H/('Evidence/original/'+suite+'.json')).read_text());new=json.loads((H/(group+'_compat.json')).read_text());a=old['cases'];b=new['cases'];idsa=[r['id'] for r in a];idsb=[r['id'] for r in b]
 assert len(set(idsa))==len(idsa) and len(set(idsb))==len(idsb)
 assert set(idsa)==set(idsb)
 out=Path(new['artifact_root'])
 for x,y in zip(a,b):
  assert x['id']==y['id'];id=x['id'];trace=None;first=None;oldpath=snap/'output'/folder/(('trace_'+str(id).zfill(2)+'.json') if group=='usb' else str(id)+'_trace.json')
  candidate=json.loads((out/(group+'_'+str(id)+'.json')).read_text())
  if oldpath.exists():
   prior=json.loads(oldpath.read_text());ot=prior['baseline']['trace'];nt=candidate['trace'];trace=ot==nt
   for k in range(max(len(ot),len(nt))):
    oa=ot[k] if k<len(ot) else None;na=nt[k] if k<len(nt) else None
    if oa!=na:first={'index':k,'baseline':oa,'candidate':na};break
  # 无完整原Python谓词证据就保持失败关闭，不以C返回0替代。
  passed=bool(trace is True and candidate['assertions_pass'] and y.get('authorization_completed'))
  rows.append({'suite':suite,'id':id,'original_pass':x['pass'],'C_assertions_pass':candidate['assertions_pass'],'authorization_completed':y.get('authorization_completed'),'trace_equal':trace,'full_gate_pass':passed,'classification':'D' if not passed else 'equivalent','first_difference':first,'baseline_trace':str(oldpath),'candidate_trace':str(out/(group+'_'+str(id)+'.json'))})
(H/'full_id_trace_gate.json').write_text(json.dumps({'missing_ids':[],'new_ids':[],'duplicate_ids':[],'python_special_predicates_not_proven':True,'gate_pass':False,'cases':rows},indent=2))
# 使用真实ARMCC5生成ABI字段，不将文档常数当成测量结果。
os.environ['C1_TEST_OUTPUT_ROOT']=str(ROOT/'output/B2B6/final_abi');sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.build('abi',[ROOT/'Tests/ConsumerIntegrationC2_2B/layout_probe.c'],'Layout_Run')
syms,sections=c.images['abi'];address=syms['g_c2b_abi'];data=None
for a,b in sections:
 if a<=address and address+40<=a+len(b):data=b[address-a:address-a+40]
assert data is not None
abi=list(struct.unpack('<10I',data));assert abi==[232,144,332,824,56,52,24,40,144,200]
manifest=json.loads((H/'final_build.json').read_text());maps=[]
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
(H/'abi_map_stack.json').write_text(json.dumps({'ABI':abi,'columns':['consumer','tx','rx','qualification','lifecycle_message','AppMessage','CameraFrame','CameraDebug','request_offset','generation_offset'],'map':maps,'stack_verified_on_hardware':False,'all_indirect_call_depth_proven':False,'baseline_RAM_delta_not_remeasured':True},indent=2))
print('ABI',abi,'gate rejected',len([r for r in rows if not r['full_gate_pass']]),'of',len(rows))
