"""Archive only verified build and software test records, without images or device data."""
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1'
DEST = ROOT / 'Tests/QspiFlashQ1/h1/evidence'

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write(path, data):
    out = DEST / path
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2) + '\n', encoding='utf-8')

def copy(path, target):
    out = DEST / target
    out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(path, out)

for role in ('backup', 'write'):
    folder = OUT / 'h1/evidence' / role
    build = read(folder / 'build.json')
    assert build['profile'] == role and build['errors'] == build['warnings'] == 0
    assert build['axf_sha256'] == sha(folder / build['axf'])
    log = folder / 'h7_mac_CM7.log'
    assert build['log_sha256'] == sha(log)
    for path, expected in build['sources'].items():
        assert sha(ROOT / path) == expected
    assert sha(ROOT / 'MDK-ARM/h7_mac.uvprojx') == build['original_project_sha256']
    copy(folder / 'build.json', f'{role}/build.json')
    copy(log, f'{role}/h7_mac_CM7.log')
    mock = OUT / 'h1/mock' / role
    result = read(mock / 'results.json')
    assert result['passed'] == result['cases'] and result['source_sha256'] == sha(ROOT / 'CM7/BSP/bsp_qspi.c')
    copy(mock / 'results.json', f'{role}/mock.json')
    copy(mock / 'commands.json', f'{role}/mock_commands.json')

host = OUT / 'h1/host_tests.log'
host_text = host.read_text(encoding='utf-8-sig', errors='replace').replace('\x00', '')
assert re.search(r'Ran 8 tests', host_text) and 'OK' in host_text and 'skipped' not in host_text
copy(host, 'host_tests.log')
rollback = ROOT / 'output/QspiFlashQ1/q13/current/MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf'
prior = read(ROOT / 'Tests/QspiFlashQ1/q13/evidence/build/results.json')['builds'][0]
assert prior['target'] == 'h7_mac_CM7' and sha(rollback) == prior['axf_sha256']
write('rollback_candidate.json', dict(role='local Q1.3 M7 rollback candidate; deployed image unverified',
    path=rollback.relative_to(ROOT).as_posix(), loads=prior['loads'], sha256=prior['axf_sha256']))
regression = {}
for name, rel in [('q11', 'mock'), ('q12', 'q12/mock'), ('q13', 'write_mock')]:
    case = read(OUT / rel / 'results.json')
    assert case['passed'] == case['cases'] and case['source_sha256'] == sha(ROOT / 'CM7/BSP/bsp_qspi.c')
    regression[name] = {k:case[k] for k in ('cases', 'passed', 'assertions')}
gate = read(OUT / 'q13/hal_gate/results.json')
assert gate['closed_gate']['pass_'] and gate['init_timeout']['pass_']
write('regression.json', dict(mock=regression, hal=gate))
write('scope.json', dict(hardware_run=False, actual_backup_captured=False, actual_ram_package_generated=False,
    production_window_bytes=0, write_candidate_only=True,
    candidate_window=[0x00FFF000, 0x01000000], adjacent_read_only=[0x00FFE000, 0x00FFF000]))
write('manifest.json', {p.relative_to(DEST).as_posix(): sha(p) for p in sorted(DEST.rglob('*'))
                        if p.is_file() and p.name != 'manifest.json'})
