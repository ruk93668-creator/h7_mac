import json, hashlib
from pathlib import Path
base=Path(__file__).resolve().parent
r=json.loads((base/'Camera/results.json').read_text(encoding='utf-8'))
b=json.loads((base/'Camera/baseline_results.json').read_text(encoding='utf-8'))
prior=json.loads((base.parent/'CameraCleanup/Camera/results.json').read_text(encoding='utf-8'))
assert r['summary']['failed']==0
assert r['cases'][54]['write_trace_sha256']==b['cases'][0]['write_trace_sha256']==prior['cases'][0]['write_trace_sha256']
y8=r['cases'][54]['write_trace']
for index, expected in [(54, []),(55,[0x4300,0x501F]),(56,[0x4300])]:
 trace=r['cases'][index]['write_trace']
 assert len(trace)==len(y8)==272
 changes=[(a>>8) for a,b in zip(y8,trace) if a!=b]
 assert changes==expected,(index,changes)
print('PASS: accepted Y8 SHA256 identical; 272 writes in identical order; RGB565/YUV422 differ only at format registers')
