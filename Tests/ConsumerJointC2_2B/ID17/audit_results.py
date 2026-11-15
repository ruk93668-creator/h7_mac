"""Audit complete IDs, preserved failures, real Maps and exact source boundaries."""
from pathlib import Path
import json,hashlib,subprocess,sys,os,time,struct,re,collections,shutil
H=Path(__file__).resolve().parent;ROOT=H.parents[2];P=H.parent;D=H/'Evidence'
old=P/'ID7/Evidence/regression';fresh=D/'history';comparisons=[]
for suite in ['c1','c1_1','c2_1_1','c2a','r2','b1','b1_media','b1_integration','b2','b3','business','consumer','power','i2c']:
 a=json.loads((old/(suite+'.json')).read_text());b=json.loads((fresh/(suite+'.json')).read_text())
 if isinstance(a,list):a={'cases':a,'summary':{'total':len(a),'passed':sum(x['pass'] for x in a)}}
 if isinstance(b,list):b={'cases':b,'summary':{'total':len(b),'passed':sum(x['pass'] for x in b)}}
 key=lambda r:json.dumps([r.get(k) for k in ['group','optimization','id','case']])
 ka=[key(r) for r in a['cases']];kb=[key(r) for r in b['cases']];am={key(r):r for r in a['cases']};bm={key(r):r for r in b['cases']}
 missing=sorted(set(ka)-set(kb));added=sorted(set(kb)-set(ka));duplicates=[k for k,n in collections.Counter(kb).items() if n>1]
 transitions=[{'id':k,'previous':am[k]['pass'],'current':bm[k]['pass']} for k in set(ka)&set(kb) if am[k]['pass']!=bm[k]['pass']]
 assert not(missing or added or duplicates or transitions) and ka==kb
 failure_details=[]
 for k in am:
  if not am[k]['pass']:
   fields=['return','failure_line','error','failure','concurrent_state','baseline_ok','current_ok']
   left={f:am[k][f] for f in fields if f in am[k]};right={f:bm[k][f] for f in fields if f in bm[k]}
   assert left==right
   failure_details.append({'case':k,'previous':left,'current':right,'unchanged':True})
 comparisons.append({'suite':suite,'failure_details':failure_details,'IDs':len(kb),'missing':missing,'added':added,'duplicate':duplicates,'order_equal':True,'result_changes':transitions,'current_summary':b['summary'],'ID_check':'PASS','raw_suite_status':'PASS' if all(r['pass'] for r in b['cases']) else 'FAIL'})
ga=(old/'gate.log').read_text(encoding='utf-8',errors='replace');gb=(fresh/'gate.log').read_text(encoding='utf-8',errors='replace')
# The historical gate JSON has totals only, so compare its actual unittest IDs.
pattern=r'^(test_\w+) \(([^)]+)\) \.\.\. (\w+)'
ka=re.findall(pattern,ga,re.M);kb=re.findall(pattern,gb,re.M)
assert len(ka)==29 and ka==kb and len(set(x[:2] for x in kb))==29 and all(x[2]=='ok' for x in kb)
comparisons.append({'suite':'Regression Gate','IDs':len(kb),'missing':[],'added':[],'duplicate':[],'order_equal':True,'result_changes':[],'ID_check':'PASS','raw_suite_status':'PASS','case_IDs':kb})
(H/'regression_id_comparison.json').write_text(json.dumps(comparisons,indent=2))
legacy=json.loads((P/'legacy_153_gate.json').read_text());assert sum(r['status']=='BLOCKED' for r in legacy['cases'])==128
runs=json.loads((H/'execution.json').read_text());assert len(runs['runs'])==13 and all(r['exit_as_expected'] for r in runs['runs'])
probes=json.loads((H/'results.json').read_text())['cases']
assert all(r['expectation_matched'] for r in probes)
assert all(r['call_counts'].get('App_CameraQualificationStart')==1 and r['call_counts'].get('App_CameraQualificationReceive')==4 for r in probes if r['group']=='task')
consumer=json.loads((D/'consumer_results.json').read_text());business=json.loads((D/'business_business_results.json').read_text())
assert consumer['summary']=={'total':44,'passed':42,'failed':2}
fail=[r for r in consumer['cases'] if not r['pass']];assert len(fail)==2 and all(r['id']==17 for r in fail)
ids=[r for r in business['cases'] if r['group']=='camera_task' and r['id']==5];assert len(ids)==2 and all(r['pass'] for r in ids)
assert business['summary']['passed']==62
for opt in ['O0','O2']:
 rows=json.loads((D/(f'b2_{opt}_usb_compat.json')).read_text())['cases'];assert len(rows)==102 and all(r['assertions_pass'] for r in rows)
 assert rows[10]['usb_consumer_words'][54:56]==[4,1]
for name in ['id7_results.json','id7_joint_results.json']:
 d=json.loads((D/name).read_text());assert d['executions']==22 and d['passed']==22
assert all(r['status']=='PASS' for r in json.loads((D/'contracts_contracts.json').read_text())['cases'])
assert all(r['pass'] for r in json.loads((D/'exit_exit_results.json').read_text())['cases'])
# Preserve all fresh C entry raw traces for B2 O0/O2; no legacy gate promotion.
snapshot=Path(runs['snapshot'])
for opt in ['O0','O2']:
 m=json.loads((D/f'b2_{opt}_usb_compat.json').read_text());out=Path(m['artifact_root']);target=D/'b2_traces'/opt;target.mkdir(parents=True,exist_ok=True)
 import gzip
 for f in out.glob('usb_*.json'):
  with gzip.open(target/(f.name+'.gz'),'wb') as z:z.write(f.read_bytes())
# Rebuilt Maps must match ID7 after-fix baseline; also retain pre-ID7 comparison.
now=json.loads((D/'resources_abi_map_stack.json').read_text());baseline=json.loads((P/'ID7/abi_map_stack.json').read_text());pre=json.loads((P/'abi_map_stack.json').read_text());build=json.loads((D/'build_build.json').read_text())
assert all(t['exit']==0 and t['errors']==0 and t['warnings']==0 for t in build['targets'])
comparisons=[]
for a,b,c0 in zip(now['map'],baseline['map'],pre['map']):
 assert a['totals'][:5]==b['totals'][:5] and a['symbols']==b['symbols'] and a['stack']==b['stack']
 comparisons.append({'target':a['target'],'current_totals':a['totals'],'ID7_post_fix_totals':b['totals'],'ID7_pre_fix_totals':c0['totals'],'runtime_columns_delta_from_8a83591':[x-y for x,y in zip(a['totals'][:5],b['totals'][:5])],'debug_bytes_delta':a['totals'][5]-b['totals'][5],'RW_delta_from_pre_fix':a['totals'][3]-c0['totals'][3],'ZI_delta_from_pre_fix':a['totals'][4]-c0['totals'][4],'stack':a['stack'],'symbols':a['symbols']})
# AXF debug data can contain snapshot paths. Compare actual loadable sections too.
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
oldbuild=json.loads((P/'ID7/build.json').read_text());loadable=[]
def load_hash(path):
 with Path(path).open('rb') as f:
  e=ELFFile(f);parts=sorted([(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']])
  return hashlib.sha256(b''.join(struct.pack('<II',a,len(b))+b for a,b in parts)).hexdigest()
for a,b in zip(build['targets'],oldbuild['targets']):
 x=load_hash(a['artifacts']['axf']['path']);y=load_hash(b['artifacts']['axf']['path']);assert x==y
 loadable.append({'target':a['target'],'baseline_loadable_SHA256':y,'current_loadable_SHA256':x,'match':True})
# ABI sizes and offsets from fresh ARMCC5 output, not source estimates.
OUT=ROOT/'output/ConsumerJoint'/('ID17_abi_'+str(time.time_ns()));os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT);sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
c.build('layout',[H/'layout_probe.c'],'ID17_Layout');syms,sections=c.images['layout'];addr=syms['g_layout'];values=None
for a,b in sections:
 if a<=addr and addr+112<=a+len(b):values=list(struct.unpack('<28I',b[addr-a:addr-a+112]))
expected=[232,144,332,824,56,52,24,144,204,220,12,16,28,40,160,276,144,808,20,36,40,44,1,4,8,12,16,28];assert values==expected
columns=['Consumer','Tx','Rx','Qualification','LifecycleMessage','AppMessage','CameraFrame','Consumer.request','Consumer.generation','Consumer.granted','Tx.allocated_through','Tx.issued_through','Tx.pending','Rx.replay','Rx.reply_pending','Rx.candidate','Qualification.rx','Qualification.session_id','Lifecycle.identity','Lifecycle.related','Lifecycle.flags','Lifecycle.result','AppMessage.source','AppMessage.command_id','AppMessage.stream_epoch','AppMessage.lease_id','AppMessage.result','AppMessage.frame']
(H/'resources.json').write_text(json.dumps({'measured_ABI':dict(zip(columns,values)),'loadable_sections':loadable,'Maps':comparisons,'AppUsbContext':json.loads((D/'resources_resource_delta.json').read_text())['AppUsbContext'],'RAM_delta':0,'all_indirect_worst_case_stack_proven':False,'hardware_stack_verified':False,'build':build,'ABI_artifact_root':str(OUT)},indent=2))
shutil.copy2(OUT/'commands.json',D/'abi_compile_commands.json')
g=['git','-c','safe.directory='+ROOT.as_posix()]
assert not subprocess.check_output(g+['diff','8a83591','--','CM7','CM4','Board','Common','MDK-ARM'],cwd=ROOT)
changed=subprocess.check_output(g+['diff','8a83591','--name-only'],cwd=ROOT).decode().splitlines()
assert all(p.startswith('Tests/ConsumerJointC2_2B/ID17/') for p in changed)
(H/'audit_summary.json').write_text(json.dumps({'ID17_production_path':'PASS','Core_batch_boundary':'EXPECTED_REJECT','raw_Consumer_suite':'FAIL 42/44 preserved','C2_2B_complete_gate':'BLOCKED','historical_128_BLOCKED':'retained, not promoted','production_changes':[],'source_hashes':{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ['CM7/Application/app_camera_qualification.c','CM7/Application/Tasks/camera_task.c','Tests/ConsumerIntegrationC2_2B/consumer_test.c','Tests/ConsumerIntegrationC2_2B/camera_task_test.c']},'C2_2C':'NOT_RUN'},indent=2))
print('PASS: source/ID/failure preservation; positive serial admission; ABI/Map/RAM/Stack; no production changes')
