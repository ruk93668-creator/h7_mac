from pathlib import Path
import os,sys,json,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/V2Phase3/negative';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as a
for d in ['Lifecycle','Supervisor']:a.cc+=['-I',ROOT/'CM7/ApplicationV2'/d]
changes=[
 ('drain',14,'Lifecycle/managed_lifecycle.c',r'z = c->ops->prove\(c->port, &c->request, c->completion.fact\);','z = 0;'),
 ('identity',10,'Lifecycle/managed_lifecycle.c',r' \|\| !LC_IdEqual\(id, &c->request.id\)',''),
 ('first_error',44,'Supervisor/execution_manager.c',r'if \(z && !c->first_error\)','if (z)'),
 ('stop_order',32,'Supervisor/execution_plan.c',r'j = ix\[k - 1\];','j = ix[c->modules.count-k];')]
rows=[]
for opt in ['O0','O2']:
 a.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in a.cc]
 for name,case,rel,pattern,new in changes:
  target=ROOT/'CM7/ApplicationV2'/rel;source=target.read_text();assert re.search(pattern,source),name
  p=OUT/(name+opt+'.c');p.write_bytes(re.sub(pattern,new,source,count=1).encode())
  sources=[p if s==target else s for s in sorted((ROOT/'CM7/ApplicationV2').rglob('*.c'))]
  a.build(name+opt,[HERE/'execution_test.c',*sources],'EM_Run')
  v=a.execute(name+opt,'EM_Run',case);rows.append({'mutation':name,'opt':opt,'id':case,'detected':not v['pass'] and v['failure_line']>0 and not v['error'],**v})
summary={'total':len(rows),'detected':sum(x['detected'] for x in rows)}
(HERE/'negative_results.json').write_bytes(json.dumps({'summary':summary,'cases':rows},indent=2).encode())
print(summary);raise SystemExit(any(not x['detected'] for x in rows))
