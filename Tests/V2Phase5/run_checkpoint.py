from pathlib import Path
import os,sys,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent;OUT=ROOT/'output/V2Phase5/checkpoint';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT);sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as a
rows=[]
for opt in ['O0','O2']:
 a.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in a.cc]
 for name in ['usb_format','usb_consumer','usb_frame']:
  a.run(a.cc+['-c',ROOT/('CM7/ApplicationV2/Modules/USB/'+name+'.c'),'-o',OUT/(name+opt+'.o')])
 a.build('consumer'+opt,[HERE/'consumer_smoke.c',ROOT/'CM7/ApplicationV2/Modules/USB/usb_consumer.c',ROOT/'CM7/ApplicationV2/Resource/media_source.c',ROOT/'CM7/Application/app_camera_lifecycle.c',ROOT/'CM7/Application/app_camera_lifecycle_replay.c',ROOT/'CM7/Application/app_camera_qualification.c'],'USB_Smoke')
 for i in range(8):rows.append({'opt':opt,'id':i,**a.execute('consumer'+opt,'USB_Smoke',i)})
s={'total':len(rows),'pass':sum(x['pass'] for x in rows)}
(HERE/'checkpoint_results.json').write_bytes(json.dumps({'summary':s,'cases':rows,'scope':'client smoke only; NOT qualification resource proof'},indent=2).encode());print(s);raise SystemExit(any(not x['pass'] for x in rows))
