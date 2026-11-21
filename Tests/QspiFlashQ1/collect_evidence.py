"""Freeze reviewable text/JSON evidence; binaries remain in independent output."""
from pathlib import Path
import json, hashlib, shutil

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1'
DEST = Path(__file__).resolve().parent / 'evidence'
DEST.mkdir(exist_ok=True)
files = ['baseline_build.json', 'verified_build.json', 'scope_audit.json',
         'mock/results.json', 'mock/commands.json', 'mock/run.log',
         'hal_link/results.json', 'hal_link/commands.json', 'hal_link/link_map.log',
         'regression/results.json']
files += [v + '/' + t + '.log' for v in ['baseline', 'verified'] for t in ['h7_mac_CM7', 'h7_mac_CM4']]
files += [p.relative_to(OUT).as_posix() for p in (OUT / 'regression').glob('*.log')]
checks = []
for suite in ['SdramRange', 'DcmiReplaceTarget', 'UsbUvcDescriptors']:
    rows = [json.loads((OUT / v / 'Tests' / suite / 'results.json').read_text(encoding='utf-8'))
            for v in ['baseline', 'verified']]
    if suite == 'SdramRange':
        same = {k:v for k,v in rows[0].items() if k != 'command'} == {k:v for k,v in rows[1].items() if k != 'command'}
        summaries = [{k:r[k] for k in ['total', 'pass', 'fail']} for r in rows]
    elif suite == 'DcmiReplaceTarget':
        same = rows[0]['cases'] == rows[1]['cases']
        summaries = [r['summary'] for r in rows]
    else:
        summaries = [r['summary'] for r in rows]
        same = summaries[0] == summaries[1] and all('camera_mjpeg_fifo.h' in
            (OUT / 'regression' / (v + '_UsbUvcDescriptors.log')).read_text(errors='replace')
            for v in ['baseline', 'verified'])
    checks.append(dict(suite=suite, same_observed_results=same, summaries=summaries))
    assert same
(OUT / 'regression/comparison.json').write_text(json.dumps(checks, indent=2))
files.append('regression/comparison.json')
manifest = {}
for rel in files:
    target = DEST / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(OUT / rel, target)
    manifest[rel] = hashlib.sha256(target.read_bytes()).hexdigest()
(DEST / 'manifest.json').write_text(json.dumps(manifest, indent=2))
print('Evidence files:', len(manifest), 'Baseline/current historical failures match.')
