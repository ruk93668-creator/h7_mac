"""Generate source-backed individual records; never promote changed legacy contracts to PASS."""
from pathlib import Path
import sys,json,csv,gzip,re,hashlib,collections,importlib.util,shutil
H=Path(__file__).resolve().parent;ROOT=H.parents[2];spec=importlib.util.spec_from_file_location('ev',H/'evaluate.py');ev=importlib.util.module_from_spec(spec);spec.loader.exec_module(ev)
gate=json.loads((H/'RawResults/Frozen/153_gate.json').read_text());contracts=json.loads((H/'contracts.json').read_text());by={(r['suite'],r['id'],r['optimization']):r for r in contracts['cases']}
records=H/'Records';records.mkdir(exist_ok=True);rows=[];sources=[]
prod={'b2':['CM7/Application/app_usb.c','CM7/Application/Tasks/usb_task.c','CM7/Application/app_camera_consumer.c'], 'b3':['CM7/Application/app_storage.c','CM7/Application/Tasks/storage_task.c','CM7/Service/jpeg_frame.c','CM7/Service/mjpeg_avi.c']}
def read(p):
 b=(ROOT/p).read_bytes()
 try:return b.decode('utf-8')
 except UnicodeDecodeError:return b.decode('gbk')
def definitions(path):
 text=read(path);out=[]
 for m in re.finditer(r'(?m)^[\w \t*]+\b(\w+)\([^;{}]*?\)\s*\{',text):
  if m[1] in ['if','for','while','switch','sizeof']:continue
  out.append({'function':m[1],'file':path,'line':text[:m.start()].count('\n')+1})
 return out
def function_range(text,name):
 m=re.search(r'\b'+name+r'\([^;{}]*\)\s*\{',text);assert m
 a=text.index('{',m.start());depth=1;i=a+1
 while depth and i<len(text):
  if text[i]=='{':depth+=1
  elif text[i]=='}':depth-=1
  i+=1
 return text[:m.start()].count('\n')+1,text[:i].count('\n')+1,text[m.start():i]
# Independent old-version replay is evaluated against the very same new causal predicates.
unique=[]
for opt in ['O0','O2']:
 old=json.load(gzip.open(H/'RawResults/OldID7Reproduction'/f'usb_candidate_{opt}_10.json.gz','rt'));a=ev.load('usb','baseline',opt,10);new=ev.load('usb','candidate',opt,10)
 old_eval=ev.evaluate(a,old);new_eval=ev.evaluate(a,new);assert old_eval['status']=='FAIL' and new_eval['status']=='PASS'
 unique.append({'optimization':opt,'old_C_return':old['return'],'old_semantic_status':old_eval['status'],'old_first_contract_violation':old_eval['first_contract_violation'],'old_consumer_phase_grant':old['final_consumer'][54:56],'current_status':new_eval['status'],'current_phase_grant':new['final_consumer'][54:56],'old_raw':f'RawResults/OldID7Reproduction/usb_candidate_{opt}_10.json.gz','current_raw':f'RawResults/Current/usb_candidate_{opt}_10.json.gz'})
(H/'UniqueFail.json').write_text(json.dumps({'original':'b2/10','root_closed':True,'production_fix_this_round':False,'prior_fix':'8a83591','reproduction':unique},indent=2))
for original in gate['cases']:
 suite,id=original['suite'],original['id'];group='usb' if suite=='b2' else 'storage';folder='UsbModuleB2' if suite=='b2' else 'StorageModuleB3';path='Tests/'+folder+'/behavior_test.c'
 a=ev.load(group,'baseline','O0',id);b=ev.load(group,'candidate','O0',id);text=read(path);start,end,body=function_range(text,b['entry']);case=b['case'];scenario=case%32 if suite=='b2' else case
 conditions=[]
 for n,line in enumerate(text.splitlines(),1):
  if not start<=n<=end:continue
  m=re.search(r'if\(([^{};]+?)\)',line)
  if m and ('scenario' in m[1] or 'id' in m[1]):
   expression=m[1].replace('||',' or ').replace('&&',' and ')
   try:matched=bool(eval(expression,{'__builtins__':{}},{'id':case,'scenario':scenario}))
   except Exception:matched=None
   if matched is not False:conditions.append({'line':n,'condition':m[1],'static_match':matched,'source':line.strip()})
 for check in b['checks']:
  original_line=text.splitlines()[check['line']-1]
  compact=lambda x:re.sub(r'\s+','',x)
  assert compact(check['expression']) in compact(original_line),(suite,id,check['line'],check['expression'],original_line)
 pair=[by[suite,id,opt] for opt in ['O0','O2']];assert all(x['status']=='PASS' for x in pair)
 equal=[ev.load(group,'baseline',o,id)['trace']==ev.load(group,'candidate',o,id)['trace'] for o in ['O0','O2']]
 current_only=(suite=='b2' and id>=96) or(suite=='b3' and id>=47)
 final='PASS' if all(equal) or current_only else 'CONTRACT_CHANGED'
 categories=['B: missing complete transport/assertion/state evidence supplied']
 if final=='CONTRACT_CHANGED':categories.append('D: historical full-event trace identity no longer holds; new causal contract executed O0/O2')
 if suite=='b2' and id<96 and id%32==22:categories.append('A: startup ACK must complete before repeated-START fault injection; fixture corrected')
 if current_only:categories.append('A: original Python uses candidate-only/negative-baseline/ownership-count predicate, not baseline full-trace equality')
 if suite=='b2' and id==10:categories.append('C: genuine old-version omitted RELEASE reproduced, absent on current production; prior fix retained')
 first=None
 for i in range(max(len(a['trace']),len(b['trace']))):
  x=a['trace'][i] if i<len(a['trace']) else None;y=b['trace'][i] if i<len(b['trace']) else None
  if x!=y:first={'index':i,'baseline':x,'current':y};break
 defs=[d for p in prod[suite] for d in definitions(p)]
 trace_origins=[]
 for entry_def in definitions(path):
  try:lo,hi,fn_body=function_range(text,entry_def['function'])
  except Exception:continue
  for code in set(int(x) for x in re.findall(r'Trace\((\d+),',fn_body)):
   count=sum(t[0]==code for t in b['trace'])
   if count:trace_origins.append({'code':code,'observed_calls':count,'dependency_or_test_function':entry_def['function'],'file':path,'line':lo})
 details={'original_suite':suite,'original_id':id,'name':original['name'],'original_status':original['status'],'original_assertions':({'C_executed':b['checks'],'Python':'baseline.ok && candidate.ok && full trace equality'} if not current_only else {'C_executed':b['checks'],'Python':('current.ok' if suite=='b2' or id==47 else 'candidate.ok && !baseline.ok' if id in [48,49] else 'baseline.ok && candidate.ok && current_owner_checks > baseline_owner_checks')}),'test_entry':{'file':path,'function':b['entry'],'case_argument':case,'mode_input_from_trace':sorted(set(r[1] for r in b['trace'] if suite=='b2' and r[0]==14)),'source_start_line':start,'source_end_line':end,'matched_source_clauses':conditions},'production_source':defs,'runtime_trace_origins':trace_origins,'compiled_real':prod[suite]+['CM7/Application/app_camera_lifecycle.c','CM7/Application/app_camera_lifecycle_replay.c','CM7/Application/app_camera_qualification.c'],'mock_boundary':('USB Mode/Core/UVC physical transport, Bridge idle, RTOS; actual App_USB owns Consumer' if suite=='b2' else 'StorageFS medium, FIFO lease boundary, RTOS; actual Storage/JPEG/AVI'),'preconditions':['Fresh zeroed ARM image per case','Original BOOT/owner/config/fault inputs from the actual matched source clauses','QUERY/ACQUIRE through actual Tx/Rx/Replay/Qualification before dependent Mode Start/Mount when requested','No test writes to granted/phase/qualification/Replay'], 'original_missing_evidence':original['evidence_gaps'],'original_first_unable_reason':original['evidence_gaps'][0] if original['evidence_gaps'] else ('confirmed historical ID7 production root' if original['status']=='FAIL' else None),'first_legacy_trace_difference':first,'software_verifiable':True,'hardware_needed_for_this_unit_contract':False,'hardware_limit':'Real ISO/DMA/SD/FIFO timing is outside these historical unit fixtures; listed separately as HARDWARE_REQUIRED, never claimed by software','classification':categories,'current_status':final,'new_contract_O0':'PASS','new_contract_O2':'PASS','causal_check_names':sorted(set(c['name'] for x in pair for c in x['checks'])),'original_exact_trace_equal_O0_O2':equal,'current_first_contract_violation':None,'next_action':('Review/approve the explicit old/new contract mapping; do not relabel old FAIL as PASS' if final=='CONTRACT_CHANGED' else 'No remaining software unit evidence gap; retain hardware limitations'),'raw_O0':f'RawResults/Current/{group}_candidate_O0_{id}.json.gz','raw_O2':f'RawResults/Current/{group}_candidate_O2_{id}.json.gz'}
 (records/f'{suite}_{id:03d}.json').write_text(json.dumps(details,ensure_ascii=False,indent=2),encoding='utf-8')
 rows.append({'suite':suite,'id':id,'name':original['name'],'original_status':original['status'],'current_status':final,'classification':' | '.join(categories),'test_entry':b['entry'],'case_argument':case,'source':path+':'+str(start),'original_assertions':json.dumps(b['checks'],ensure_ascii=False),'preconditions':' | '.join(details['preconditions']),'missing_evidence_before':' | '.join(original['evidence_gaps']),'first_unable_reason_before':details['original_first_unable_reason'],'software_verifiable':True,'hardware_for_unit':False,'O0':'PASS','O2':'PASS','next_action':details['next_action'],'record':f'Records/{suite}_{id:03d}.json'})
with (H/'Inventory.csv').open('w',encoding='utf-8-sig',newline='') as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
summary={'total':153,'final_status_counts':dict(collections.Counter(r['current_status'] for r in rows)),'original_128':dict(collections.Counter(r['current_status'] for r in rows if r['original_status']=='BLOCKED')),'unique_FAIL_root_closed':True,'current_production_defects_found':0,'production_modified':False,'new_contract_executions':306,'new_contract_PASS':306,'new_contract_checks':sum(len(r['checks']) for r in contracts['cases']),'hardware_required_within_unit_scope':0,'hardware_product_scope':'separate HARDWARE_REQUIRED matrix; NOT_RUN','remaining_software_BLOCKED':0,'legacy_failures_preserved':True,'signed_off_full_gate':False}
(H/'summary.json').write_text(json.dumps(summary,indent=2))
source=['# Source Mapping','','The original C predicates and failure branches were preserved. Instrumentation evaluates each requirement once and records line/expression/result; transport wrappers call the unchanged original mock body and record its actual return. Qualification/Consumer/Replay source and production code were not changed.','', 'Compiled real modules versus dependency mocks are explicit in every Records/*.json. USB Bridge/UVC are stubs in these legacy unit fixtures; real Bridge/CameraTask integration is separately re-run by Joint/ID7/ID17. This report does not claim hardware or C2.2C release evidence.','']
for r in rows:
 d=json.loads((H/r['record']).read_text());source += [f"## {r['suite']}/{r['id']} — {r['name']}",f"Entry `{r['test_entry']}({r['case_argument']})` at `{r['source']}`.",f"Source clauses: {json.dumps(d['test_entry']['matched_source_clauses'],ensure_ascii=False)}",f"Executed original requirements: {json.dumps(d['original_assertions'],ensure_ascii=False)}",f"Production definitions: {json.dumps(d['production_source'],ensure_ascii=False)}",f"Observed dependency call origins/counts: {json.dumps(d['runtime_trace_origins'],ensure_ascii=False)}",f"Contract checks: {', '.join(d['causal_check_names'])}",f"Result: **{r['current_status']}**; O0/O2 new causal contracts PASS. [Independent record]({r['record']})",'']
(H/'SourceMapping.md').write_text('\n'.join(source),encoding='utf-8')
matrix=['# Evidence Matrix','', '| ID | Test | Old | New classification | O0/O2 | Independent record |','|---|---|---|---|---|---|']
for r in rows:matrix.append(f"| {r['suite']}/{r['id']} | {r['name']} | {r['original_status']} | {r['current_status']} | PASS / PASS | [{r['suite']}_{r['id']}]({r['record']}) |")
(H/'EvidenceMatrix.md').write_text('\n'.join(matrix)+'\n',encoding='utf-8');print(summary)
