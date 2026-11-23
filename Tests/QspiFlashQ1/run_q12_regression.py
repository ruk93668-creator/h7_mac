"""Retain historical failures and compare case details against frozen input."""
from pathlib import Path
import subprocess, sys, os, json, concurrent.futures, re
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1/q12'
SUITES = ['SdramRange', 'DcmiReplaceTarget', 'UsbUvcDescriptors']
DEST = OUT / 'regression'
DEST.mkdir(exist_ok=True)
def check(item):
    variant, suite = item
    tree = OUT / variant
    env = dict(os.environ, PYTHONPATH=str(ROOT / 'output/QspiFlashQ1/python_deps'), PYTHONIOENCODING='utf-8')
    p = subprocess.run([sys.executable, str(tree / 'Tests' / suite / 'run_tests.py')],
                       cwd=tree, env=env, capture_output=True, timeout=180)
    label = variant + '_' + suite
    (DEST / (label + '.log')).write_bytes(p.stdout + p.stderr)
    result = json.loads((tree / 'Tests' / suite / 'results.json').read_text(encoding='utf-8'))
    (DEST / (label + '.json')).write_text(json.dumps(result, indent=2))
    return dict(variant=variant, suite=suite, exit=p.returncode, result=result)
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
    rows = list(pool.map(check, [(v, s) for v in ['baseline', 'current'] for s in SUITES]))
comparison = []
for suite in SUITES:
    pair = [r['result'] for r in rows if r['suite'] == suite]
    if suite == 'SdramRange':
        same = pair[0]['cases'] == pair[1]['cases'] and pair[0]['compiled_values'] == pair[1]['compiled_values']
        summaries = [{k: p[k] for k in ['total', 'pass', 'fail']} for p in pair]
    elif suite == 'DcmiReplaceTarget':
        same = pair[0]['cases'] == pair[1]['cases']
        summaries = [p['summary'] for p in pair]
    else:
        #去除只有临时绝对路径不同的诊断；保留异常类型、缺失头文件及全部 Core 用例。
        def normalize(text, variant):
            tree = str(OUT / variant)
            text = text.replace(tree, '<TREE>').replace(tree.replace('\\', '/'), '<TREE>')
            text = re.sub(r'UsbUvcDescriptors_[A-Za-z0-9_]+', 'UsbUvcDescriptors_<TEMP>', text)
            return re.sub(r'Ran (\d+) tests in [0-9.]+s', r'Ran \1 tests in <DURATION>s', text)
        same = pair[0]['summary'] == pair[1]['summary'] and pair[0]['core_cases'] == pair[1]['core_cases']
        same = same and normalize(pair[0]['output'], 'baseline') == normalize(pair[1]['output'], 'current')
        summaries = [p['summary'] for p in pair]
    comparison.append(dict(suite=suite, same_observed_results=same, summaries=summaries))
(DEST / 'results.json').write_text(json.dumps([{k: v for k, v in r.items() if k != 'result'} for r in rows], indent=2))
(DEST / 'comparison.json').write_text(json.dumps(comparison, indent=2))
print(json.dumps(comparison, indent=2))
sys.exit(not all(r['same_observed_results'] for r in comparison))
