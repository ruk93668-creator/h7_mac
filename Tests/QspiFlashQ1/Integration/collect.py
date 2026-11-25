"""Collect text software evidence; leave AXF/HEX and local user files outside Git."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from build import ROOT

work=Path(sys.argv[1]).resolve()
evidence=ROOT/'Tests/QspiFlashQ1/Integration/evidence'
evidence.mkdir(exist_ok=False)
def copy(source,destination):
    target=evidence/destination
    target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(source,target)

for name in ('build01','mock02','audit02','bsp01','v3_01','history04'):
    for p in (work/name).glob('*.json'):
        copy(p,Path(name)/p.name)
    for p in (work/name).glob('*.log'):
        copy(p,Path(name)/p.name)
for p in (work/'build01').glob('*/result.json'):copy(p,Path('build01')/p.parent.name/p.name)
for p in (work/'build01').glob('*/rebuild.log'):copy(p,Path('build01')/p.parent.name/p.name)
for p in (work/'bsp01/tree/output/QspiFlashQ1').rglob('*'):
    if p.is_file() and p.suffix in ('.json','.log'):
        copy(p,Path('bsp_results')/p.relative_to(work/'bsp01/tree/output/QspiFlashQ1'))
for folder in (work/'v3_01').iterdir():
    if folder.is_dir() and folder.name not in ('tree','historical_tree'):
        for p in folder.rglob('*'):
            if p.is_file() and p.suffix in ('.json','.log'):
                copy(p,Path('v3_results')/p.relative_to(work/'v3_01'))
for p in (work/'history04').glob('*/results.json'):copy(p,Path('history04')/p.parent.name/p.name)
for name in ('history01','history02','history03'):
    p=work/name/'commands.json'
    if p.exists():copy(p,Path('initial_harness_failures')/name/'commands.json')

env=None
cmd=['python','Tests/QspiFlashQ1/h2/test_verify_report.py',
     'Tests/QspiFlashQ1/h2/evidence/download_scope.json','output/QspiFlashQ1/h2/mock']
r=subprocess.run(cmd,cwd=work/'bsp01/tree',capture_output=True,env=env)
(evidence/'h2_pc_retest.log').write_bytes(r.stdout+r.stderr)
assert r.returncode==0
(evidence/'invocation_corrections.json').write_text(json.dumps([
    dict(original='bsp01/core_10.log',reason='required manifest/mock-output arguments omitted',resolved='h2_pc_retest.log',exit=0),
    dict(original='v3_01/product_06.log',reason='historical trace test requires the isolated BoardProfiles overlay',resolved='v3_results/usb_trace_retest/results.json',exit=0),
    dict(original='initial_harness_failures',reason='FIFO runner needed an unused host mmap stub and both ARMCC standard assert entry forms',resolved='history04/comparison.json',exit=0)
],indent=2)+'\n')
manifest={str(p.relative_to(evidence)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
          for p in evidence.rglob('*') if p.is_file()}
(evidence/'sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
package=work/'coexistence_candidate01.zip'
with zipfile.ZipFile(package,'w',compression=zipfile.ZIP_DEFLATED) as z:
    for name in ('isolated_test','candidate_cm4'):
        for p in (work/'build01'/name).iterdir():
            if p.suffix in ('.axf','.hex','.map','.htm'):
                z.write(p,name+'/'+p.name)
    z.write(work/'build01/image_manifest.json','image_manifest.json')
    z.writestr('DOWNLOAD_NOT_AUTHORIZED.txt','Software review candidate only. No probe or board download was performed. Independent permission is required.\n')
print(json.dumps(dict(text_evidence_files=len(manifest),package=str(package),sha256=hashlib.sha256(package.read_bytes()).hexdigest()),indent=2))
