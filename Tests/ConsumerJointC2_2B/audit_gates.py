"""Complete ID sets plus conservative per-case causal evidence; preserve historic FAIL."""
from pathlib import Path
import json,collections,gzip,hashlib
H=Path(__file__).resolve().parent;ROOT=H.parents[1];OLD=ROOT/'Tests/ConsumerIntegrationB2B6'
reports=[]
for suite in ['c1','c1_1','c2_1_1','c2a','r2','b1','b1_media','b1_integration','b2','b3','business','consumer']:
 a=json.loads((OLD/'Evidence/original'/f'{suite}.json').read_text(encoding='utf-8'));b=json.loads((H/'Evidence/regression'/f'{suite}.json').read_text(encoding='utf-8'))
 def key(r):return json.dumps([r.get(k) for k in ['group','optimization','id','case']],sort_keys=True)
 ka=[key(r) for r in a['cases']];kb=[key(r) for r in b['cases']];am={key(r):r for r in a['cases']};bm={key(r):r for r in b['cases']}
 missing=sorted(set(ka)-set(kb));added=sorted(set(kb)-set(ka));duplicate=[k for k,v in collections.Counter(kb).items() if v>1]
 transitions=[{'key':k,'baseline':am[k]['pass'],'candidate':bm[k]['pass']} for k in am.keys()&bm.keys() if am[k]['pass']!=bm[k]['pass']]
 reports.append({'suite':suite,'baseline_count':len(ka),'candidate_count':len(kb),'missing':missing,'added':added,'duplicate':duplicate,'order_equal':ka==kb,'outcome_transitions':transitions,'ID_gate':'PASS' if not(missing or added or duplicate or transitions) else 'FAIL','current_summary':b.get('summary'),'note':'ID gate PASS is not suite PASS; original failures retained'})
(H/'regression_id_gate.json').write_text(json.dumps(reports,indent=2))
# 每条原始/候选轨迹独立封存，153项全量核对，不用zip截断
oldgate=json.loads((OLD/'full_id_trace_gate.json').read_text());lookup={(r['suite'],r['id']):r for r in oldgate['cases']}
run=json.loads((H/'Evidence/regression/runs.json').read_text());snapshot=Path(run[0]['snapshot'])
rows=[];dest=H/'Evidence/legacy_pairs';dest.mkdir(exist_ok=True)
for group,suite,folder in [('usb','b2','UsbModuleB2'),('storage','b3','StorageModuleB3')]:
 original=json.loads((H/'Evidence/regression'/f'{suite}.json').read_text());compat=json.loads((H/(group+'_compat.json')).read_text());orig={r['id']:r for r in original['cases']};candidate={r['id']:r for r in compat['cases']}
 assert len(candidate)==len(compat['cases']) and set(orig)==set(candidate)
 for id in sorted(orig):
  x,y=orig[id],candidate[id];old=lookup[suite,id]
  path=snapshot/'output'/folder/(f'trace_{id:02d}.json' if group=='usb' else f'{id}_trace.json')
  base=json.loads(path.read_text()) if path.exists() else None
  new=json.loads((Path(compat['artifact_root'])/f'{group}_{id}.json').read_text())
  #旧/候选事件保留，归档中显式标识资源Ops测试桩而非真实Camera证明
  raw={'original_test':x,'legacy_image_pair':base,'candidate':new,'qualification_resource_ops_stub':True,'real_camera_adapter':False}
  with gzip.open(dest/f'{suite}_{id:03d}.json.gz','wt',encoding='utf-8') as f:json.dump(raw,f)
  ot=base['baseline']['trace'] if base else [];nt=new['trace'];counts=lambda t:dict(collections.Counter(str(a[0]) for a in t))
  first=None
  for k in range(max(len(ot),len(nt))):
   a=ot[k] if k<len(ot) else None;b=nt[k] if k<len(nt) else None
   if a!=b:first={'legacy_record':k,'baseline':a,'candidate':b};break
  #仅继承原来已严格证明且本次仍一致的24项；绝不因C断言全绿扩大PASS
  passed=old['full_gate_pass'] and new['assertions_pass'] and y['authorization_completed'] and ot==nt
  related=suite=='b2' and id==10
  status='PASS' if passed else 'FAIL' if related else 'BLOCKED'
  classification=None if passed else 'C' if related else 'D'
  gap=[] if passed else ['Original Python full-state/Debug/ACK predicate has not been translated to a causal contract for this ID; C success alone is insufficient.']
  if not base:gap.append('No original per-image raw pair for interface/special predicate; see original runner and preserved raw result.')
  if not y['authorization_completed']:gap.append('Authorization proof incomplete in compatibility fixture.')
  violation={'contract':'USB abort failure then background stop must not omit RELEASE intent','evidence':'new real joint ID7 confirms shared App_USBFinishRound path; legacy ID10 injects abort failure then retries','source':'CM7/Application/app_usb.c:App_USBFinishRound'} if related else None
  rows.append({'suite':suite,'id':id,'name':x.get('name'),'status':status,'classification':classification,'secondary_architecture_difference':'A: asynchronous QUERY/ACQUIRE adds legal scheduling steps' if first else None,'historical_legacy_pass':x['pass'],'previous_full_gate_pass':old['full_gate_pass'],'current_C_assertions_pass':new['assertions_pass'],'authorization_completed':y['authorization_completed'],'full_gate_pass':passed,'first_contract_violation':violation,'legacy_first_difference_is_not_contract_violation':first,'evidence_gaps':gap,'baseline_call_counts':counts(ot),'candidate_call_counts':counts(nt),'trace':f'Evidence/legacy_pairs/{suite}_{id:03d}.json.gz'})
summary={'total':len(rows),'previous_PASS':sum(x['full_gate_pass'] for x in oldgate['cases']),'status_counts':dict(collections.Counter(x['status'] for x in rows)),'classification_counts':dict(collections.Counter(x['classification'] for x in rows if x['classification'])),'no_automatic_promotion':all(not r['full_gate_pass'] or r['previous_full_gate_pass'] for r in rows),'C_assertion_count':sum(x['current_C_assertions_pass'] for x in rows)}
(H/'legacy_153_gate.json').write_text(json.dumps({'summary':summary,'missing_ids':[],'new_ids':[],'duplicate_ids':[],'gate_pass':all(r['status']=='PASS' for r in rows),'cases':rows},indent=2))
lines=['# B2/B3 153项逐项复核','',f'本轮统计：{summary}。历史完整轨迹门禁24 PASS/129未通过记录原样保留。','', 'C断言通过不等于完整验收；正常新增异步事件不作为first_contract_violation。BLOCKED表示该ID的原Python诊断/结果/交错谓词尚未获得等价契约证据，不是已证明的生产缺陷。','', '| Suite/ID | 场景 | C断言 | 完整验收 | 分类 | 原始轨迹 |','|---|---|---|---|---|---|']
for r in rows:lines.append(f"| {r['suite']}/{r['id']} | {r['name']} | {'PASS' if r['current_C_assertions_pass'] else 'FAIL'} | {r['status']} | {r['classification'] or '—'} | [{r['id']}]({r['trace']}) |")
(H/'Legacy153Review.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
print(summary)
assert all(r['ID_gate']=='PASS' for r in reports)
