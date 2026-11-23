"""Collect text evidence only, after the independent Q1.3 checks pass."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1'
DEST = ROOT / 'Tests/QspiFlashQ1/q13/evidence'
BASE = 'b5c79ecb14956cd04dc2823c34273ca2be70d4da'

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def write(rel, value):
    out = DEST / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

def copy(source, rel):
    out = DEST / rel
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(OUT / source, out)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

cpath = 'CM7/BSP/bsp_qspi.c'
hpath = 'CM7/BSP/bsp_qspi.h'
new = (ROOT / cpath).read_text(encoding='utf-8')
old = subprocess.check_output(['git', 'show', BASE + ':' + cpath], cwd=ROOT).decode('utf-8').replace('\r\n', '\n')
policy = r'typedef struct\n\{\n    uint32_t address, bytes;\n\} QspiWritePolicy_t;\n\n//[^\n]+\nstatic const QspiWritePolicy_t s_write_policy = \{0U, 0U\};\n\n'
prefix, mutation = new.split('#define QSPI_PROGRAM_BUSY_LIMIT_MS', 1)
fixed, count = re.subn(policy, '', prefix)
assert count == 1 and old.rstrip() == fixed.rstrip(), 'Review existing read-only code'
cmd_literals = sorted(set(re.findall(r'Qspi_WriteCommand\(write, (0x[0-9A-F]+U)', mutation) +
                          re.findall(r'Qspi_WriteCommand\(&write, (0x[0-9A-F]+U)', mutation)))
assert cmd_literals == ['0x02U', '0x06U', '0x20U']
source_paths = subprocess.check_output(['git', 'diff', BASE, '--name-only', '--',
    'CM7', 'CM4', 'Common', 'Drivers', 'Board'], cwd=ROOT, text=True).splitlines()
assert sorted(source_paths) == [cpath, hpath]
assert 'write_test.c' not in (ROOT / 'MDK-ARM/h7_mac.uvprojx').read_text(encoding='utf-8')
assert not re.search(r'\b(malloc|calloc|realloc|free|pvPortMalloc|xTaskCreate|xSemaphoreCreate)\s*\(', mutation)
write('scope.json', dict(production=source_paths, preexisting_read_logic_unchanged=True,
    private_policy={'address': 0, 'bytes': 0}, mutation_commands=cmd_literals,
    no_dynamic_allocation_or_task=True, hardware_run=False))

runs = {'q11': 'mock', 'q12': 'q12/mock', 'sfdp': 'sfdp_mock',
        'quad30': 'quad_id_mock/30', 'quad120': 'quad_id_mock/120', 'write': 'write_mock'}
totals = {}
for name, folder in runs.items():
    d = read(OUT / folder / 'results.json')
    assert d['cases'] == d['passed']
    assert d['source_sha256'] == sha(ROOT / cpath)
    assert d['assertions'] == sum(r['assertions'] for r in d['rows'])
    totals[name] = {k:d[k] for k in ('cases', 'passed', 'assertions', 'source_sha256')}
write('mock_summary.json', totals)
for filename in ('results.json', 'commands.json', 'run.log'):
    copy(f'write_mock/{filename}', f'write/{filename}')
gate = read(OUT / 'q13/hal_gate/results.json')
assert gate['closed_gate']['pass_'] and gate['init_timeout']['pass_']
for filename in ('results.json', 'commands.json', 'link_map.log'):
    copy(f'q13/hal_gate/{filename}', f'hal_gate/{filename}')
comparison = read(OUT / 'q13/regression/comparison.json')
assert all(r['same_observed_results'] for r in comparison)
for path in (OUT / 'q13/regression').glob('*'):
    if path.suffix == '.log' or path.name in ('comparison.json', 'results.json'):
        copy(f'q13/regression/{path.name}', f'regression/{path.name}')

build = read(OUT / 'q13/build.json')
for rel in (cpath, hpath):
    assert sha(ROOT / rel) == sha(OUT / 'q13/current' / rel)
for row in build['builds']:
    assert row['errors'] == row['warnings'] == 0 and not row['write_symbols']
    filename = row['target'] + '.log'
    assert row['log_sha256'] == sha(OUT / 'q13/current' / filename)
    copy(f'q13/current/{filename}', f'build/{filename}')
copy('q13/build.json', 'build/results.json')
write('manifest.json', {p.relative_to(DEST).as_posix(): sha(p) for p in sorted(DEST.rglob('*'))
                        if p.is_file() and p.name != 'manifest.json'})
