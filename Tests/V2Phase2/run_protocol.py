from pathlib import Path
import os,sys,json,subprocess
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/V2Phase2/protocol';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT);sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as a
rows=[]
a.build('protocol',[ROOT/'Tests/CameraLifecycleC1/protocol_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c'],'C1_ProtocolRun')
for i in range(70):rows.append({'suite':'C1','group':'protocol','id':i,**a.execute('protocol','C1_ProtocolRun',i)})
old=OUT/'old_messages.c';old.write_bytes(subprocess.check_output(['git','show','35b4fb7f5c39f7339b0bf0aa2a68594fd9748e7f:CM7/Application/app_messages.c'],cwd=ROOT))
for name,source,defs in [('baseline',old,['-DC1_BASELINE']),('current',ROOT/'CM7/Application/app_messages.c',[])]:
 a.build(name,[ROOT/'Tests/CameraLifecycleC1/transport_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c'],'C1_LegacyRun',defs+['-DC1_MESSAGES_SOURCE="'+source.as_posix()+'"'])
for i in range(60):
 oldr=a.execute('baseline','C1_LegacyRun',i);new=a.execute('current','C1_LegacyRun',i)
 rows.append({'suite':'C1','group':'legacy','id':i,'pass':oldr['pass'] and new['pass'] and oldr['trace']==new['trace'],'baseline':oldr,'current':new})
for group,entry,count in [('transport','C1_TransportRun',49),('permission','C1_PermissionRun',240)]:
 for i in range(count):rows.append({'suite':'C1','group':group,'id':i,**a.execute('current',entry,i)})
a.build('reliability',[ROOT/'Tests/CameraLifecycleC1_1/reliability_test.c',ROOT/'CM7/Application/app_camera_lifecycle.c'],'C1_1_ReliabilityRun',['-DC1_MESSAGES_SOURCE="'+(ROOT/'CM7/Application/app_messages.c').as_posix()+'"'])
for i in range(24):rows.append({'suite':'C1.1','group':'reliability','id':i,**a.execute('reliability','C1_1_ReliabilityRun',i)})
summary={s:{'total':sum(x['suite']==s for x in rows),'pass':sum(x['suite']==s and x['pass'] for x in rows),'fail':sum(x['suite']==s and not x['pass'] for x in rows)} for s in ['C1','C1.1']}
(HERE/'protocol_results.json').write_text(json.dumps({'summary':summary,'cases':rows},indent=2),encoding='utf-8');print(summary);raise SystemExit(any(not x['pass'] for x in rows))
