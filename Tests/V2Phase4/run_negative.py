from pathlib import Path
import os,sys,json,re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/V2Phase4/negative';OUT.mkdir(parents=True,exist_ok=True)
os.environ['C1_TEST_OUTPUT_ROOT']=str(OUT)
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'));sys.path.insert(0,str(ROOT/'output/V2Phase1/python_deps'))
import run_tests as a
a.cc+=['--split_sections','-I',ROOT/'CM7/Device/Components/OV5640','-I',ROOT/'CM7/ApplicationV2/Modules/Camera']
changes=[
 ('sensor_veto',5,'camera_format.c','result = Ov5640_ValidateConfig(&native_config);','result = SYS_OK;'),
 ('drain_proof',72,'camera_module.c','const struct V2LifecycleOps VCamera_LifecycleOps = {lc_owner, can_begin, begin, prove};','static SysResult_t unchecked(void *p,const LCRequest *r,uint32_t f){(void)prove(p,r,f);return 0;}\nconst struct V2LifecycleOps VCamera_LifecycleOps = {lc_owner,can_begin,begin,unchecked};'),
 ('fresh_hardware_fact',73,'camera_module.c','(camera->result.observation.flags & observation.flags)','(camera->result.observation.flags)'),
 ('dispatch_once',74,'camera_module.c','camera->submitted = 1;','camera->submitted = 0;'),
 ('full_reply_identity',75,'camera_endpoint.c','if (!LC_IdEqual(&reply.completion.id, id))','if (0)')]
rows=[]
for opt in ['O0','O2']:
 a.cc=[('-'+opt if x in ['-O0','-O2'] else x) for x in a.cc]
 for name,case,rel,old,new in changes:
  target=ROOT/'CM7/ApplicationV2/Modules/Camera'/rel;original=target.read_text(encoding='utf-8');assert old in original,name
  mutant=OUT/(name+opt+'.c');mutant.write_bytes(original.replace(old,new,1).encode())
  sources=[mutant if s==target else s for s in sorted((ROOT/'CM7/ApplicationV2').rglob('*.c'))]
  sources += [HERE/'native_dependencies.c',ROOT/'CM7/Device/Src/ov5640_device.c',ROOT/'CM7/Media/Src/camera_capture.c']
  a.build(name+opt,[HERE/'camera_test.c',*sources],'Camera_Run')
  v=a.execute(name+opt,'Camera_Run',case);rows.append({'mutation':name,'opt':opt,'id':case,'detected':not v['pass'] and v['failure_line']>0 and not v['error'],**v})
summary={'total':len(rows),'detected':sum(x['detected'] for x in rows)}
(HERE/'negative_results.json').write_bytes(json.dumps({'summary':summary,'cases':rows},indent=2).encode());print(summary)
raise SystemExit(any(not x['detected'] for x in rows))
