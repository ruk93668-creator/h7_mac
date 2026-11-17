from pathlib import Path
import sys,os,json,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/V2Phase2/negative';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT);sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as a
a.cc+=['-I',ROOT/'CM7/ApplicationV2/Resource']
source=(ROOT/'CM7/ApplicationV2/Resource/media_resource.c').read_text()
changes=[('dma',17,r'z\s*=\s*c->source.ops->readers_quiescent\(c->source.context,\s*&r->g,\s*m\);\s*if\s*\(z\)\s*return leave\(c,\s*z\);','z=SYS_OK;'),('close',8,r'a->closed\s*=\s*1;','a->closed=0;'),('ack',21,r'if\s*\(!t\s*\|\|\s*!t->enqueued\)','if(!t)')]
rows=[]
for opt in ['O0','O2']:
 a.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in a.cc]
 for name,case,old,new in changes:
  assert re.search(old,source)
  p=OUT/(name+'_'+opt+'.c');p.write_text(re.sub(old,new,source,count=1),encoding='utf-8')
  a.build(name+opt,[HERE/'resource_test.c',p,ROOT/'CM7/ApplicationV2/Resource/media_source.c'],'MR_Run')
  r=a.execute(name+opt,'MR_Run',case);rows.append({'mutation':name,'opt':opt,'case':case,'detected':not r['pass'] and r['failure_line']>0 and not r['error'],**r})
summary={'total':len(rows),'detected':sum(x['detected'] for x in rows)}
(HERE/'negative_results.json').write_text(json.dumps({'summary':summary,'cases':rows},indent=2),encoding='utf-8');print(summary);raise SystemExit(any(not x['detected'] for x in rows))
