"""Observe original Core ID17 and actual CameraTask ID5; no production modifications."""
from pathlib import Path
import sys,os,time,json,gzip,struct,hashlib,subprocess
H=Path(__file__).resolve().parent;ROOT=H.parents[2]
BASE='8a83591b4baa0a850ccdfb269b68d752af5bee81'
g=['git','-c','safe.directory='+ROOT.as_posix()]
assert not subprocess.check_output(g+['diff',BASE,'--','CM7','Tests/ConsumerIntegrationC2_2B','Tests/ConsumerQualificationC2_2A','Tests/CameraModuleB1'],cwd=ROOT)
OUT=ROOT/'output/ConsumerJoint'/('ID17_probe_'+str(time.time_ns()));OUT.mkdir(parents=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT);sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));import run_tests as c
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_PC
common=[ROOT/'CM7/Application'/n for n in ['app_camera_consumer.c','app_camera_lifecycle.c','app_camera_lifecycle_replay.c']]
defs=['-DB1_APP_SOURCE="'+(ROOT/'CM7/Application/app_camera.c').as_posix()+'"','-DB1_TASK_SOURCE="'+(ROOT/'CM7/Application/Tasks/camera_task.c').as_posix()+'"']

def execute(path,group):
 with path.open('rb') as f:
  e=ELFFile(f);syms={s.name:(s['st_value'],s['st_size'],s['st_info']['type']) for s in e.get_section_by_name('.symtab').iter_symbols()}
  sections=[(s['sh_addr'],s.data()) for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x80000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,b in sections:u.mem_write(a,b)
 def word(a):return struct.unpack('<I',u.mem_read(a,4))[0]
 def val(name):return word(syms[name][0]) if name in syms else None
 L=list(struct.unpack('<15I',u.mem_read(syms['g_id17_layout'][0],60)))
 def signed(x):return x if x<2**31 else x-2**32
 def snap():
  q=syms['core'][0] if group=='core' else syms['s_camera'][0]+L[13]
  clients=[]
  for i in range(2):
   a=syms['clients'][0]+i*L[0];clients.append({'phase':word(a+L[1]),'result':signed(word(a+L[2])),'session':word(a+L[3]),'generation':word(a+L[4]),'granted':word(a+L[5]),'request_id':word(a+L[6])})
  counts={}
  if group=='task':
   count=val('g_trace_count');trace=[list(x) for x in struct.iter_unpack('<25I',u.mem_read(syms['g_trace'][0],min(count,1024)*100))]
   counts={'Device_Start':sum(x[0]==22 for x in trace),'CameraCapture_StartStream':sum(x[0]==25 for x in trace),'CameraCapture_Start':sum(x[0]==26 for x in trace)}
  return {'session_id':word(q+L[7]),'starts':val('starts') if group=='core' else counts['Device_Start'],'hardware_entries':counts,'consumers':clients,'qualification_phase':[word(q+i*L[11]+L[12]) for i in range(2)],'final_reply_enqueued':[word(q+L[8]+i*L[9]+L[10]) for i in range(2)],'self_wakes':val('self_wakes'),'inbox_count':val('in_count')}
 functions={v[0]&~1:k for k,v in syms.items() if v[2]=='STT_FUNC' and k.startswith(('App_','CameraTask_','RuntimeEvent_','Device_','CameraCapture_','Qual_'))}
 events=[];rounds=[];current=[None];returns={};call_counts={}
 def hook(cpu,a,n,data):
  for fn,args,seq in returns.pop(a,[]):
   row={'sequence':len(events)+1,'event':'return','function':fn,'call_sequence':seq,'result_raw':cpu.reg_read(UC_ARM_REG_R0),'state':snap()};events.append(row)
   if fn=='App_CameraConsumerFlush' and args[0]==syms['clients'][0]+L[0] and current[0] is not None:current[0]['after_both_flushes']=snap()
   if fn=='App_CameraConsumerProcess' and args[0]==syms['clients'][0]+L[0] and current[0] is not None:
    current[0]['after']=snap();current[0]['self_wake_delta']=None if group=='core' else current[0]['after']['self_wakes']-current[0]['before']['self_wakes']
  if a not in functions:return
  fn=functions[a];args=[cpu.reg_read(r) for r in [UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3]];call_counts[fn]=call_counts.get(fn,0)+1
  if fn=='App_CameraConsumerFlush' and args[0]==syms['clients'][0]:
   current[0]={'round':len(rounds),'before':snap(),'received':[]};rounds.append(current[0])
  row={'sequence':len(events)+1,'event':'call','function':fn,'args':args,'state':snap()}
  if fn in ['App_LifecycleRxReceiveRequest','App_LifecycleRxComplete','App_LifecycleTxStage','App_LifecycleTxAcceptReply']:
   try:row['message']=list(struct.unpack('<14I',cpu.mem_read(args[1],56)))
   except Exception:row['unreadable_message']=True
  if fn=='App_LifecycleRxReceiveRequest' and current[0] is not None:current[0]['received'].append({'sequence':row['sequence'],'message':row.get('message'),'inbox_after_dequeue':row['state']['inbox_count']})
  events.append(row);returns.setdefault(cpu.reg_read(UC_ARM_REG_LR)&~1,[]).append((fn,args,row['sequence']))
 u.hook_add(UC_HOOK_CODE,hook);entry='ID17_Core' if group=='core' else 'ID17_Task';id=17 if group=='core' else 5
 u.reg_write(UC_ARM_REG_SP,0x2007fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,id);error=None
 try:u.emu_start(syms[entry][0]|1,0x01000000,count=20000000)
 except Exception as ex:error=str(ex)
 raw={'return':u.reg_read(UC_ARM_REG_R0),'failure_line':val('g_failure'),'error':error,'returned':u.reg_read(UC_ARM_REG_PC)==0x01000000}
 raw['original_status']='PASS' if raw['returned'] and not raw['return'] and not raw['failure_line'] and not error else 'FAIL'
 final=snap();classification='EXPECTED_REJECT' if group=='core' else 'PASS'
 if group=='core':
  expected=raw['original_status']=='FAIL' and final['session_id']==0 and final['starts']==0 and [x['phase'] for x in final['consumers']]==[6,6] and [x['result'] for x in final['consumers']]==[-5,-5] and final['qualification_phase']==[0,0] and final['final_reply_enqueued']==[1,1]
 else:
  expected=raw['original_status']=='PASS' and final['session_id']>0 and final['starts']==1 and final['qualification_phase']==[2,2] and all(x['granted']==1 and x['session']==final['session_id'] for x in final['consumers']) and final['self_wakes']==4 and len(rounds)==8 and [len(r['received']) for r in rounds]==[1,1,1,1,0,0,0,0] and [r['self_wake_delta'] for r in rounds]==[1,1,1,1,0,0,0,0] and rounds[0]['after_both_flushes']['inbox_count']==2 and final['hardware_entries']['CameraCapture_StartStream']==1
 return {'group':group,'original_id':id,'raw':raw,'acceptance_status':classification if expected else 'FAIL','expectation_matched':expected,'final':final,'rounds':rounds,'call_counts':call_counts,'events':events,'layout':L,'hardware_real':False}

rows=[];D=H/'ProbeEvidence';D.mkdir(exist_ok=True)
for opt in ['O0','O2']:
 c.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in c.cc]
 for group in ['core','task']:
  sources=[H/(group+'_probe.c'),*common]
  if group=='task':sources += [ROOT/'CM7/Application/app_camera_qualification.c',ROOT/'Tests/ConsumerIntegrationC2_2B/legacy_transport_fixture.c']
  name=group+'_'+opt;c.build(name,sources,'ID17_Core' if group=='core' else 'ID17_Task',defs)
  r=execute(OUT/(name+'.axf'),group)
  with gzip.open(D/(name+'.json.gz'),'wt',encoding='utf-8') as f:json.dump(r,f)
  row={k:v for k,v in r.items() if k!='events'};row['optimization']=opt;row['raw_trace']='ProbeEvidence/'+name+'.json.gz';rows.append(row);print(group,opt,r['raw'],r['acceptance_status'],r['final'],flush=True)
(H/'results.json').write_text(json.dumps({'cases':rows,'executions':len(rows),'baseline':BASE,'snapshot_sources_unchanged':True,'artifact_root':str(OUT),'core_original_FAIL_preserved':True},indent=2))
raise SystemExit(any(not x['expectation_matched'] for x in rows))
