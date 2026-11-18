"""Run the established regression runners without overwriting their prior evidence."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = ROOT / 'Tests/V2Phase4'
OUT = ROOT / 'output/V2Phase5/regression'
OUT.mkdir(parents=True, exist_ok=True)
RUNNERS = ['run_regression.py', 'run_resource.py', 'run_execution.py', 'run_tests.py',
           'run_protocol.py', 'run_negative.py']
if len(sys.argv) == 2:
    name = sys.argv[1]
    assert name in RUNNERS
    script = HERE / name
    source = script.read_text(encoding='utf-8')
    source = source.replace("ROOT/'output/V2Phase4", "ROOT/'output/V2Phase5/regression")
    source = re.sub(r"\(HERE/'([^']+\.json)'\)", lambda m: f"(Path({str(OUT / m[1])!r}))", source)
    # Previous tests use globs. Keep their original source scope below USB and
    # validate the new module through the independent Phase5 link.
    source = source.replace("sorted((ROOT/'CM7/ApplicationV2').rglob('*.c'))",
                            "sorted(p for p in (ROOT/'CM7/ApplicationV2').rglob('*.c') if p.parent.name != 'USB')")
    exec(compile(source, str(script), 'exec'), {'__file__': str(script), '__name__': '__main__'})
else:
    rows = []
    for script in RUNNERS:
        log = OUT / (script + '.log')
        with log.open('wb') as f:
            p = subprocess.run([sys.executable, __file__, script], cwd=ROOT, stdout=f, stderr=subprocess.STDOUT)
        rows.append({'runner': script, 'exit': p.returncode,
                     'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest()})
        print(rows[-1], flush=True)
    (OUT / 'index.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    raise SystemExit(any(r['exit'] for r in rows))
