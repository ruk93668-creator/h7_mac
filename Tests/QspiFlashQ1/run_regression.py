"""Run selected unchanged historical suites only inside isolated build snapshots."""
from pathlib import Path
import subprocess, sys, os, json, concurrent.futures

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1'
SUITES = ['Tests/SdramRange/run_tests.py', 'Tests/DcmiReplaceTarget/run_tests.py',
          'Tests/UsbUvcDescriptors/run_tests.py']

def check(item):
    variant, suite = item
    dest = OUT / variant
    env = dict(os.environ)
    env['PYTHONPATH'] = str(OUT / 'python_deps')
    env['PYTHONIOENCODING'] = 'utf-8'
    p = subprocess.run([sys.executable, str(dest / suite)], cwd=dest, env=env,
                       capture_output=True, timeout=120)
    name = variant + '_' + Path(suite).parent.name
    log = OUT / 'regression' / (name + '.log')
    log.write_bytes(p.stdout + p.stderr)
    return dict(variant=variant, suite=suite, exit=p.returncode, pass_=p.returncode == 0,
                log=str(log.relative_to(ROOT)))

if __name__ == '__main__':
    (OUT / 'regression').mkdir(exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        rows = list(pool.map(check, [(v, s) for v in ['baseline', 'verified'] for s in SUITES]))
    (OUT / 'regression/results.json').write_text(json.dumps(rows, indent=2))
    print(json.dumps(rows, indent=2))
    sys.exit(not all(r['pass_'] for r in rows))
