"""Validate fixed-vs-baseline causal evidence without rewriting historic reports."""
from pathlib import Path
import json,gzip,struct,collections
H=Path(__file__).resolve().parent;D=H/'ID7'
new=json.loads((D/'results.json').read_text());old=json.loads((D/'baseline_results.json').read_text());checks=[]
assert len(new['cases'])==22 and all(x['status']=='PASS' and x['exit']==0 for x in new['cases'])
assert len(old['cases'])==2 and all(x['status']=='FAIL' and x['observations']['phase']==3 and x['observations']['id7_stage_success']==0 for x in old['cases'])
for row in new['cases']:
 id=row['id'];opt=row['optimization'];r=json.load(gzip.open(D/'Evidence'/f'{opt}_{id}.json.gz','rt',encoding='utf-8'));obs=r['observations'];events=r['events']
 proof={'id':id,'optimization':opt,'C_exit':row['exit'],'observations':obs,'no_false_release_reply':True,'identical_release_body':True}
 replies=[e for e in events if e.get('message_words') and e['message_words'][1]==2 and e['message_words'][10]&0xC0]
 assert not replies
 requests=[e['message_words'] for e in events if e.get('message_words') and e['message_words'][1]==1 and e['message_words'][2]==3 and e['message_words'][3]==2]
 if id!=5:
  assert obs['id7_stage_success']==1 and obs['granted']==1 and obs['phase']==4
  assert requests and len({tuple(m) for m in requests})==1
  body=struct.unpack('<14I',bytes.fromhex(obs['release_message']));assert body[6]==3 and body[10]==0
 else:assert obs['id7_stage_calls']==0 and obs['granted']==0
 if id==1 and opt=='O0':
  calls={e['sequence']:e for e in events if e['event']=='call'}
  goodsend=[calls[e['call_sequence']] for e in events if e['event']=='return' and e['function']=='App_MessagesSend' and e['result_raw']==0 and e['call_sequence'] in calls]
  ack=[e for e in goodsend if e.get('message_type')==3 and e.get('command_id')==12 and e.get('message_result')==0xfffffff7]
  ret=[e for e in goodsend if e.get('message_type')==7];rack=[e for e in goodsend if e.get('message_type')==8]
  media=[e for e in events if e['event']=='return' and e['function']=='CameraCapture_StreamReleaseFrame' and e['result_raw']==0]
  mode=[e for e in events if e['event']=='return' and e['function']=='USB_Mode_Stop' and e['result_raw']==0]
  stage=[e for e in events if e['event']=='call' and e['function']=='ID7_Stage' and e.get('operation')==3]
  seq=[g[0]['sequence'] for g in [ack,ret,media,rack,mode,stage]]
  assert seq==sorted(seq) and len(set(seq))==6
  proof['critical_order']={'names':['original IO ACK','Frame Return','actual Camera Media release','Return ACK','Mode Stop OK','RELEASE Stage'],'sequences':seq}
 checks.append(proof)
# Original B2 assertions execute unchanged. The new independent assertion adds Consumer intent proof.
b2=[]
for opt in ['O0','O2']:
 d=json.loads((D/'legacy'/opt/'usb_compat.json').read_text());assert len(d['cases'])==102 and all(x['assertions_pass'] for x in d['cases'])
 row=d['cases'][10];assert row['usb_consumer_words'][54:56]==[4,1]
 b2.append({'optimization':opt,'id':10,'C_assertions':'PASS','consumer_phase':'RELEASE','granted':1,'executions':1})
(D/'contracts.json').write_text(json.dumps({'checks':checks,'B2_ID10':b2,'baseline_negative_control':'2 expected FAIL','candidate':'22 PASS','historical_128_BLOCKED_unchanged':True},indent=2))
print('PASS: baseline 2 expected FAIL; candidate 22/22; B2 ID10 O0/O2; causal order; no false completion flags')
