from pathlib import Path
import os,sys,time,struct,json,hashlib
H=Path(__file__).resolve().parent; R=H.parents[2]
O=R/'output/ConsumerJoint'/('B2B3_layout_'+str(time.time_ns()));os.environ['C1_TEST_OUTPUT_ROOT']=str(O)
sys.path.insert(0,str(R/'Tests/CameraLifecycleC1'));import run_tests as c
c.build('layout',[H.parent/'ID17/layout_probe.c'],'ID17_Layout')
syms,sections=c.images['layout'];a=syms['g_layout'];data=next(b[a-s:a-s+112] for s,b in sections if s<=a<s+len(b))
v=list(struct.unpack('<28I',data));ref=json.loads((H/'Resources.json').read_text())['related_offset_reference'];assert v==list(ref.values())
assert all(x['exit']==0 and 'warning' not in x['output'].lower() for x in c.commands)
(H/'RawResults/layout_commands.json').write_text(json.dumps(c.commands,indent=2),encoding='utf-8')
(H/'Layout.json').write_text(json.dumps({'source':'Tests/ConsumerJointC2_2B/ID17/layout_probe.c','ARMCC5':True,'actual':dict(zip(ref,v)),'baseline_equal':True,'artifact':str(O),'sha256':hashlib.sha256((O/'layout.axf').read_bytes()).hexdigest()},indent=2),encoding='utf-8')
print('PASS: 28 ARMCC5 sizes and offsets remeasured, baseline equal')
