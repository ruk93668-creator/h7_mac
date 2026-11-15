"""Per-ID causal verification; historic exact-trace FAIL remains immutable."""
from pathlib import Path
import json,gzip,struct,collections,re
H=Path(__file__).resolve().parent;ROOT=H.parents[2];D=H/'RawResults/Current'
import sys
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
meta=json.loads((H/'probe_results.json').read_text());out=Path(meta['artifact_root']);rows=[]
def load(group,version,opt,id):return json.load(gzip.open(D/f'{group}_{version}_{opt}_{id}.json.gz','rt',encoding='utf-8'))
def signed(x):return x if x<2**31 else x-2**32
def message(m):
 b=bytes.fromhex(m['bytes'])
 if m['life']:
  w=struct.unpack('<14I',b);return {'kind':w[1],'op':w[2],'source':w[3],'target':w[4],'session':w[5],'request':w[6],'consumer':w[7],'generation':w[8],'flags':w[10],'result':signed(w[11]),'raw':w}
 return {'type':b[0],'source':b[1],'command':struct.unpack('<I',b[4:8])[0],'result':struct.unpack('<i',b[16:20])[0]}
def evaluate(a,b):
 t=b['trace'];tests=[];suite=b['suite'];id=b['id'];group='usb' if suite=='b2' else 'storage';opt=b['optimization']
 def ck(name,value,evidence):tests.append({'name':name,'pass':bool(value),'evidence':evidence})
 ck('original C requirements execute without failure/drop',b['pass'] and all(x['pass'] for x in b['checks']) and b['audit_dropped']==0,{'return':b['return'],'checks':b['checks'],'loop_exit':b['loop_exit']})
 life=[(i,m,message(m)) for i,m in enumerate(b['messages']) if m['life']];received={};reqs=[]
 for i,m,p in life:
  role=2 if p['consumer']==1 else 3
  ck('observed lifecycle format and direction are valid',p['raw'][0]==1 and p['kind'] in [1,2] and p['consumer'] in [1,2] and p['request']>0 and p['generation']>0 and ((p['source'],p['target'])==((role,1) if p['kind']==1 else (1,role))),{'event':i,'message':p})
  if p['kind']==1 and m['transport_result']==0:reqs.append((i,m,p));received[(p['op'],p['request'],p['consumer'],p['generation'])]=p
  if p['kind']==2 and m['transport_result']==0:
   q=received.get((p['op'],p['request'],p['consumer'],p['generation']));ck('reply matches complete request identity',q is not None and (p['session']==q['session'] or p['op']==1) and p['target']==q['source'] and p['source']==1,{'event':i,'reply':p})
 ck('no fabricated released/hardware stopped reply',not any(p['kind']==2 and p['flags']&0xc0 for _,_,p in life),[p for _,_,p in life])
 q=[(i,m,p) for i,m,p in life if p['kind']==2 and p['op']==1 and p['result']==0];g=[(i,m,p) for i,m,p in life if p['kind']==2 and p['op']==2 and p['result']==0]
 if reqs:
  ck('QUERY reply precedes ACQUIRE and uses the granted session',q and g and q[0][0]<g[0][0] and q[0][2]['session']==g[0][2]['session']>0 and q[0][2]['generation']==g[0][2]['generation'],[p for _,_,p in life])
 if suite=='b3':ck('all original baseline requirement sites remain executed',len(b['checks'])>=len(a['checks']),{'baseline_checks':len(a['checks']),'candidate_checks':len(b['checks'])})
 if suite=='b2':
  std=[(m,message(m)) for m in b['messages'] if not m['life']]
  ck('every original standard transport call has a complete captured message',len(std)==sum(r[0]==9 for r in t),{'captured':len(std),'calls':sum(r[0]==9 for r in t)})
  for m,p in std:
   k=m['trace_index'];raw=bytes.fromhex(m['bytes']);hash=0
   for byte in raw:hash=(hash*33+byte)&0xffffffff
   ck('full ACK/FAULT body agrees with original transport hash and result',k>=3 and t[k-3][0:3]==[9,m['channel'],p['type']] and t[k-2][0:3]==[10,p['command'],hash] and signed(t[k-1][1])==p['result'] and p['source']==1,{'message':p,'trace':k,'transport_result':m['transport_result']})
  ack=[p['command'] for m,p in std if p['type']==3 and m['transport_result']==0];commands=[r[2] for r in t if r[0]==8]
  if id<96:
   ck('ACK command identities preserve consumed FIFO',all(x in commands for x in ack) and [commands.index(x) for x in ack]==sorted(commands.index(x) for x in ack),{'consumed':commands,'delivered':ack})
  delivered=[(p['type'],p['command'],p['result']) for m,p in std if m['transport_result']==0]
  ck('no duplicate successful original ACK delivery',all(n==1 for k,n in collections.Counter(x for x in delivered if x[0]==3).items()),delivered)
  for i,r in enumerate(t):
   if r[0]==18:
    idle=[j for j in range(i) if t[j][0]==24]
    ck('Mode Stop requires Bridge idle and no owned frame',idle and t[idle[-1]][1]==1 and r[25]==0,{'trace':i,'frame_owned':r[25],'idle':idle[-1] if idle else None})
   if r[0]==15 and r[1]==1:
    ck('UVC Mode Start occurs only after actual ACQUIRE reply',g and g[0][1]['trace_index']<=i,{'trace':i,'reply_trace':g[0][1]['trace_index'] if g else None})
   if r[0]==12:ck('published state/result matches original Debug sampling point',[r[1],r[2]]==[r[9],r[8]],{'trace':i,'published':r[1:3],'Debug':[r[9],r[8]]})
   if r[0]==13:ck('published EOF/FPS matches Debug sampling point',[r[1],r[2]]==[r[11],r[12]],{'trace':i,'published':r[1:3],'Debug':r[11:13]})
   if r[0]==19 and i+1<len(t):
    gap=(r[1]-r[13])&0xffffffff;z=t[i+1]
    ck('SOF coalescing/wrap uses unsigned sequence difference',z[13]==r[1] and z[14]==((r[14]+max(gap-1,0))&0xffffffff) and z[15]==max(r[15],gap),{'trace':i,'gap':gap,'before':r[13:16],'after':z[13:16]})
  boundaries=[i for i,r in enumerate(t) if r[0]==19]+[len(t)]
  for x,y in zip(boundaries,boundaries[1:]):
   seq=[r[0] for r in t[x:y] if r[0] in [21,22,23]]
   if id<96 and id//32==0:ck('Task Cancel/Process/Bridge/Process order',seq in [[22,23],[21,22,23,22]],{'range':[x,y],'events':seq})
   else:ck('TEST/NONE does not advance UVC/Bridge',not seq,{'range':[x,y],'events':seq})
  if id<96 and id//32!=0:ck('TEST/NONE does not acquire Camera',not life,len(life))
  # Recompute FPS using the actual post-GRANTED epoch, never force the old timestamp.
  for i,r in enumerate(t):
   if r[0]!=1:continue
   elapsed=(r[3]-r[22])&0xffffffff
   if elapsed<1000:continue
   later=next((z for z in t[i+1:] if z[0]!=1),None)
   if later and later[22]==r[3] and later[23]==r[28]:
    frames=(r[28]-r[23])&0xffffffff;expected=frames*1000000//elapsed if frames<=4294 else 0
    ck('FPS uses actual elapsed/EOF with overflow guard',later[12]==expected,{'trace':i,'elapsed':elapsed,'frames':frames,'expected':expected,'actual':later[12]})
  if id<96 and id%32==19:ck('task error diagnostic survives unrelated successful reads',any(r[0]==12 and signed(r[2])==-9 for r in t),None)
  if id<96 and id%32==22:ck('ACK/FAULT independent retry reaches matching terminal delivery',any(p['type']==3 and p['command']==11 for m,p in std if m['transport_result']==0) and any(p['type']==4 and p['result']==-5 for m,p in std if m['transport_result']==0),delivered)
  if id in [4,5,6]:
   expected=-4 if id==5 else -9
   ck('failed START retains original ACK and FAULT through safe Mode cleanup',any(p['type']==3 and p['command']==10 and p['result']==expected for m,p in std if m['transport_result']==0) and any(p['type']==4 and p['result']==expected for m,p in std if m['transport_result']==0) and sum(x[0]==18 for x in t)==1 and b['final_consumer'][54:56]==[4,1],delivered)
  if id in [23,31]:
   command=12 if id==23 else 13
   ck('new START is BUSY while prior grant release is pending',any(p['type']==3 and p['command']==command and p['result']==-5 for m,p in std if m['transport_result']==0) and b['final_consumer'][54:56]==[4,1] and sum(p['kind']==1 and p['op']==2 for _,_,p in life)==1,delivered)
  if id==10:
   ck('ID7/ID10 fixed path stages RELEASE while retaining grant',b['final_consumer'][54:56]==[4,1],b['final_consumer'][50:])
   io=[m for m,p in std if p['type']==3 and p['command']==11 and p['result']==-9 and m['transport_result']==0]
   ck('first STOP IO ACK still has owned frame and background stopping',io and t[io[0]['trace_index']-1][25]==1 and t[io[0]['trace_index']-1][17]==1,io)
   ck('one immutable RELEASE intent follows recovery',sum(p['kind']==1 and p['op']==3 and m['transport_result']==0 for _,m,p in life)==1,[p for _,_,p in life])
  if id<96 and id not in [4,5,6,16,23,31]:
   ck('public Debug/status milestones match original logical rounds',[(r[0],r[1],r[2],r[4:16]) for r in a['trace'] if r[0] in [12,13]]==[(r[0],r[1],r[2],r[4:16]) for r in t if r[0] in [12,13]],{'baseline':[(r[0],r[1:3],r[4:16]) for r in a['trace'] if r[0] in [12,13]],'candidate':[(r[0],r[1:3],r[4:16]) for r in t if r[0] in [12,13]]})
  essential=[15,18,21,22,23];baseline_msgs=[message(m) for m in a['messages'] if not m['life'] and m['transport_result']==0]
  same_business=(baseline_msgs==[p for m,p in std if m['transport_result']==0] and all(sum(r[0]==k for r in a['trace'])==sum(r[0]==k for r in t) for k in essential))
 else:
  with (out/'arm'/f'storage_candidate_{opt}.axf').open('rb') as f:
   elf=ELFFile(f);statussize=next(x['st_size'] for x in elf.get_section_by_name('.symtab').iter_symbols() if x.name=='g_storage_status')
  base=6+statussize//4
  if id not in [4,12,48,49]:ck('Storage Debug publish/return milestones match original',[(r[0],r[6:base]) for r in a['trace'] if r[0] in [90,91]]==[(r[0],r[6:base]) for r in t if r[0] in [90,91]],{'baseline':[(r[0],r[6:base]) for r in a['trace'] if r[0] in [90,91]],'candidate':[(r[0],r[6:base]) for r in t if r[0] in [90,91]]})
  for i,r in enumerate(t):
   if 30<=r[0]<=41:ck('filesystem operation stays in Storage owner',r[4]==4,{'trace':i,'operation':r[0],'owner':r[4]})
   if r[0] in [20,21]:ck('retain/actual SD release stays in Camera owner',r[4]==0,{'trace':i,'operation':r[0],'owner':r[4]})
   if r[0]==30:ck('Mount follows actual ACQUIRE reply',g and g[0][1]['trace_index']<=i,{'trace':i,'reply_trace':g[0][1]['trace_index'] if g else None})
  ck('commands preserve actual FIFO ordering',[x['command'] for x in a['command_events']]==[x['command'] for x in b['command_events']],{'baseline':a['command_events'],'candidate':b['command_events']})
  fs=lambda r:[x[:3] for x in r['trace'] if x[0] in [30,31,32,33,35,36,37,38,39,40,41,92]]
  same_business=fs(a)==fs(b)
  if id not in [12,48,49]:ck('filesystem calls/parameters/bytes/CRC agree with original business baseline',same_business,{'baseline':fs(a),'candidate':fs(b)})
  if id in [48,49]:ck('original Python negative baseline authority predicate',not a['pass'] and b['pass'],{'baseline_pass':a['pass'],'candidate_pass':b['pass']})
  if id==50:ck('original Python diagnostic-ready ownership-count predicate',a['pass'] and b['pass'] and sum(x[0]==5 for x in t)>sum(x[0]==5 for x in a['trace']),{'baseline_owner_calls':sum(x[0]==5 for x in a['trace']),'candidate_owner_calls':sum(x[0]==5 for x in t)})
  if id==50:ck('Task continues real Process after diagnostic ready tamper',any(x['debug_ready']==0 and x['prepared'] for x in b['process_entries']),b['process_entries'])
  if b['prepared'] or b['process_entries']:
   ck('real private status cache publishes once per valid Process and prepare',len(b['publications'])==len(b['process_entries'])+int(b['prepared']),{'publish_count':len(b['publications']),'process_count':len(b['process_entries']),'prepared':b['prepared']})
   for i,r in enumerate(t):
    if r[0]==90:
     pubs=[x for x in b['publications'] if x['trace_index']<=i]
     ck('published cache matches completed logical Process boundary',pubs and pubs[-1]['status']==r[6:base],{'trace':i,'last_publish':pubs[-1] if pubs else None})
  if id==4:ck('queue-full rejection preserves four START FIFO entries despite async admission',[x['command'] for x in b['command_events']]==[1,1,1,1] and b['final_consumer'][55]==1,b['command_events'])
  if id==12:ck('STOP before asynchronous START grant cancels mount without fake reacquire',not any(x[0]==30 for x in t) and b['final_consumer'][55]==1 and b['final_consumer'][54]==4,b['final_consumer'][50:])
  if id in [30,31]:ck('stale queue credential does not release current lease',not any(x[0]==21 for x in t) and t[-1][base]==4 and t[-1][base+13]==1 and b['final_consumer'][54:56]==[3,1],{'final_state':t[-1][base:base+14]})
  if id==33:ck('failed SD return retains ownership until later successful retry',len([x for x in t if x[0]==21])==2 and t[-1][base+13]==0,[i for i,x in enumerate(t) if x[0]==21])
  if id==35:ck('duplicate return cannot double release',len([x for x in t if x[0]==21])==1,[i for i,x in enumerate(t) if x[0]==21])
  if id in [15,16,17,25,26,27,28,29,38,39,41]:ck('failed file operation stays FAULT and is not a completed file',t[-1][base]==4 and t[-1][6+7]==0,{'state':t[-1][base],'saved_files':t[-1][13]})
  if id==40:ck('Unmount failure retains FAULT separately from already closed AVI',t[-1][base]==4 and t[-1][base+8]==1 and signed(t[-1][6+22])==0 and signed(t[-1][6+23])==-9,t[-1][base:base+14])
 failed=[x for x in tests if not x['pass']]
 return {'suite':suite,'id':id,'optimization':opt,'status':'FAIL' if failed else 'PASS','checks':tests,'first_contract_violation':failed[0] if failed else None,'same_business_baseline':same_business,'lifecycle_used':bool(reqs)}
def main():
 for suite,group,total in [('b2','usb',102),('b3','storage',51)]:
  for opt in ['O0','O2']:
   for id in range(total):rows.append(evaluate(load(group,'baseline',opt,id),load(group,'candidate',opt,id)))
 (H/'contracts.json').write_text(json.dumps({'cases':rows,'executions':len(rows),'passed':sum(x['status']=='PASS' for x in rows)},indent=2))
 for x in rows:
  if x['status']!='PASS':print(x['suite'],x['id'],x['optimization'],x['first_contract_violation']['name'])
 print('Contract runs',len(rows),'passed',sum(x['status']=='PASS' for x in rows))
 return any(x['status']!='PASS' for x in rows)

if __name__=='__main__':raise SystemExit(main())
