"""Check causal contracts, retain all raw events, never equate async traces wholesale."""
from pathlib import Path
import json,gzip,struct,collections
H=Path(__file__).resolve().parent
rows=[]
for id in range(11):
 r=json.load(gzip.open(H/'Evidence'/f'{id}.json.gz','rt',encoding='utf-8'));es=r['events'];checks=[]
 def find(fn,event='call',pred=lambda e:True):return [e for e in es if e['event']==event and e['function']==fn and pred(e)]
 def add(name,condition,evidence):checks.append({'contract':name,'status':'PASS' if condition else 'FAIL','evidence_sequences':evidence})
 def ordered(name,*groups):
  chosen=[g[0]['sequence'] if g else None for g in groups];add(name,all(n is not None for n in chosen) and all(a<b for a,b in zip(chosen,chosen[1:])),chosen)
 for cid,actor,start in [(1,'UsbTask','USB_Mode_Start'),(2,'StorageTask','StorageFS_Mount')]:
  qr=find('App_LifecycleTxAcceptReply',pred=lambda e:e.get('consumer')==cid and e.get('operation')==1)
  ar=find('App_LifecycleTxAcceptReply',pred=lambda e:e.get('consumer')==cid and e.get('operation')==2)
  granted=find('App_CameraConsumerProcess','return',lambda e:e['actor']==actor and e.get('consumer_words_after',[0]*58)[55]==1)
  ordered(f'{actor}: QUERY_REPLY < ACQUIRE_REPLY < real GRANTED < business start',qr,ar,granted,find(start))
 ordered('Camera owner prepare before any consumer START',find('App_CameraQualificationPrepare'),find('App_USBHandleCommand'))
 ordered('SD write/failed-write attempt < return pending < Camera lease return',find('StorageFS_Write') if id==4 else find('MjpegAvi_AppendFrame'),find('Storage_Release'),find('CameraMjpegFifo_ReleaseLease'))
 releases=find('CameraMjpegFifo_ReleaseLease');add('SD actual release executes only in CameraTask',bool(releases) and all(e['actor']=='CameraTask' for e in releases),[e['sequence'] for e in releases])
 usb_stop=find('App_USBHandleCommand',pred=lambda e:e.get('message_type')==2)
 returns=find('App_MessagesSend',pred=lambda e:e.get('message_type')==7)
 acks=find('App_MessagesSend',pred=lambda e:e.get('message_type')==8)
 ordered('USB STOP < cancel < Return < Camera release < Return ACK < Mode Stop',usb_stop,[e for e in find('USB_UVC_CancelFrame') if usb_stop and e['sequence']>usb_stop[0]['sequence']],returns,find('CameraCapture_StreamReleaseFrame'),acks,find('USB_Mode_Stop'))
 intent=find('App_LifecycleMessagesSend',pred=lambda e:e.get('consumer')==1 and e.get('operation')==3)
 ordered('USB complete Return/ACK < RELEASE intent (not success)',acks,intent)
 if id!=4:ordered('AVI Finish < file Close < Unmount',find('MjpegAvi_Finish'),find('StorageFS_Close'),find('StorageFS_Unmount'))
 else:add('IO failure is retained as FAULT, not normal AVI completion',r['return'] in [0,180] and not find('MjpegAvi_Finish'),[])
 stops=find('CameraCapture_Stop')+find('Device_Stop');add('No forced Camera hardware stop',not stops,[e['sequence'] for e in stops])
 success=find('App_LifecycleMessagesSend',pred=lambda e:e.get('message_words',[0]*14)[1]==2 and bool(e.get('message_words',[0]*14)[10]&0xC0))
 add('Real adapter does not fabricate released/hardware-stopped flags',not success,[e['sequence'] for e in success])
 #每轮不隐藏或改写CameraTask原调度顺序
 begins=find('CameraTask_Process');good=True
 for k,e in enumerate(begins):
  end=begins[k+1]['sequence'] if k+1<len(begins) else len(es)+1
  seq=[]
  for fn in ['App_CameraBeginRound','App_CameraProcess','App_UvcBridgeCameraProcess','App_CameraFinishRound']:
   x=[a['sequence'] for a in find(fn) if e['sequence']<a['sequence']<end];seq.append(x[0] if x else None)
  if any(n is None for n in seq) or seq!=sorted(seq):good=False
 add('Each Camera round preserves Begin/Media/Bridge/Finish',good,[e['sequence'] for e in begins])
 if id==6:
  rejected=find('App_MessagesSend',pred=lambda e:e.get('message_type')==3 and e.get('source')==0 and e.get('command_id')==11 and e.get('message_result')==0xfffffff9)
  add('ID6 exact Camera STOP ACK NOT_SUPPORTED',len(rejected)==1,[e['sequence'] for e in rejected])
 failures=[c for c in checks if c['status']=='FAIL']
 rows.append({'id':id,'status':'PASS' if r['pass'] and not failures else 'FAIL','classification':'C' if id==7 else ('D' if failures or not r['pass'] else None),'C_assertions_pass':r['pass'],'first_contract_violation':failures[0] if failures else (None if r['pass'] else {'C_check_line':r['return']}),'contracts':checks,'raw_trace':f'Evidence/{id}.json.gz','resource_release_complete':False})
(H/'contracts.json').write_text(json.dumps({'cases':rows,'summary':{'total':11,'passed':sum(x['status']=='PASS' for x in rows)},'first_contract_violation_semantics':'first unsatisfied causal obligation; normal added async events are not violations','in_flight_storage_stop_total_order':'NOT_RUN: this joint set stops Storage after frame drain; original B3 queued-stop remains separately tracked','C2_2C_real_close':'NOT_RUN'},indent=2),encoding='utf-8')
for r in rows:print(r['id'],r['status'],r['first_contract_violation'])
raise SystemExit(any(x['status']=='FAIL' for x in rows))
