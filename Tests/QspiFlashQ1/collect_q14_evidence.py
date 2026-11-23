"""Verify and archive the Q1.4 records. H1 device data stay outside Git."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

import h1_backup as h1

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1'
DEST = ROOT / 'Tests/QspiFlashQ1/q14/evidence'
BASE = '81d9b4b8b25598fc8193c3590cbb4551495c552a'
PRODUCTION = ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h']

def read(path):
    return json.loads(path.read_text(encoding='utf-8'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def save(rel, value):
    path = DEST / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

def copy(path, rel):
    target = DEST / rel
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(path, target)

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT).decode('utf-8')

paths = git('diff', BASE, '--name-only', '--', 'CM7', 'CM4', 'Common', 'Drivers', 'Board').splitlines()
assert paths == PRODUCTION
driver = (ROOT / PRODUCTION[0]).read_text(encoding='utf-8')
old_driver = git('show', BASE + ':' + PRODUCTION[0])
mutation = re.compile(r'Qspi_WriteCommand\((?:&)?write, (0x[0-9A-F]+U)')
assert sorted(mutation.findall(driver)) == sorted(mutation.findall(old_driver)) == ['0x02U','0x06U','0x20U']
assert 'static const QspiWritePolicy_t s_write_policy = {0U, 0U};' in driver
diff = git('diff', BASE, '--', *PRODUCTION)
added = '\n'.join(line[1:] for line in diff.splitlines() if line.startswith('+') and not line.startswith('+++'))
assert not re.search(r'\b(malloc|calloc|realloc|pvPortMalloc|xTaskCreate\w*|xSemaphoreCreate\w*|HAL_QSPI_\w*DMA)\s*\(', added)
assert not re.search(r'0x(?:66|99|52|D8|C7|60|01|31|11|39)U\b', added)
save('scope.json', dict(development_base='ca841b78cfca4b5b50d20741e68a3af260f13259',
    direct_baseline=BASE, production=paths, write_policy_bytes=0,
    new_mutation_opcodes=[], recovery_read_opcodes=['9F','05','35','15'], hardware_run=False))

runs = {'q11':'mock','q12':'q12/mock','q13':'write_mock','sfdp':'sfdp_mock',
        'quad30':'quad_id_mock/30','quad120':'quad_id_mock/120',
        'h1_backup':'h1/mock/backup','h1_backup120':'h1/mock/backup120','h1_write':'h1/mock/write',
        'q14':'q14/mock'}
results = {}
for name, rel in runs.items():
    run = read(OUT / rel / 'results.json')
    assert run['passed'] == run['cases'] and run['assertions'] == sum(r['assertions'] for r in run['rows'])
    assert run['source_sha256'] == sha(ROOT / PRODUCTION[0])
    results[name] = {k:run[k] for k in ('cases','passed','assertions','source_sha256')}
save('mock_summary.json', results)
for item in ('results.json','commands.json','run.log'):
    copy(OUT / 'q14/mock' / item, 'mock/' + item)
hal = read(OUT / 'q14/hal/results.json')
assert all(r['pass_'] for r in hal.values())
for item in ('results.json','commands.json','link_map.log'):
    copy(OUT / 'q14/hal' / item, 'hal/' + item)
gate = read(OUT / 'q13/hal_gate/results.json')
assert gate['closed_gate']['pass_'] and gate['init_timeout']['pass_']
copy(OUT/'q13/hal_gate/results.json','hal/q13_closed_gate.json')
host = OUT/'q14/h1_host_tests.log'
host_text = host.read_text(encoding='utf-8-sig', errors='replace').replace('\x00','')
assert 'Ran 10 tests' in host_text and 'OK' in host_text and 'skipped' not in host_text
copy(host,'h1_host_tests.log')

comparisons = read(OUT / 'q14/regression/comparison.json')
assert all(row['same_observed_results'] for row in comparisons)
for p in (OUT / 'q14/regression').iterdir():
    if p.suffix == '.log' or p.name in ('comparison.json','results.json'):
        copy(p, 'regression/' + p.name)

resources = {}
for variant in ('baseline','current'):
    b = read(OUT / 'q14' / (variant + '_build.json'))
    copy(OUT / 'q14' / (variant + '_build.json'), 'build/' + variant + '.json')
    for row in b['builds']:
        log = row['target'] + '.log'
        assert row['errors']==row['warnings']==0 and row['log_sha256']==sha(OUT/'q14'/variant/log)
        copy(OUT/'q14'/variant/log, 'build/' + variant + '/' + log)
        code,ro,rw,zi = row['sizes']
        resources.setdefault(row['target'], {})[variant] = dict(rom=code+ro+rw,
            ram=rw+zi, stack=row['maximum_static_stack'], recover_linked=row['recover_linked'])
    if variant == 'baseline':
        project_sha = b['project_sha256']
    else:
        assert b['project_sha256']==project_sha and b['source_sha256']==sha(ROOT/PRODUCTION[0])
for row in resources.values():
    row['delta'] = {k:row['current'][k]-row['baseline'][k] for k in ('rom','ram','stack')}
save('build/resources.json', resources)

field = OUT / 'h1'
original_meta,uid,original,neighbor = h1.check_archive(field/'field-backup-120')
mid_meta,mid_uid,programmed,mid_neighbor = h1.check_archive(field/'field-programmed-120')
final_meta,last_uid,erased,last_neighbor = h1.check_archive(field/'field-erased-120')
pattern = h1.test_pattern(); end = h1.OFFSET + len(pattern)
assert original_meta['board']==mid_meta['board']==final_meta['board'] and uid==mid_uid==last_uid
assert neighbor==mid_neighbor==last_neighbor and original==erased==b'\xff'*4096
assert programmed==original[:h1.OFFSET]+pattern+original[end:] and h1.crc(pattern)==0x6AFF4F17
assert read(field/'field-program-01-local/program_result.json')['confirmed']==520
assert read(field/'field-erase-01-local/erase_result.json')['confirmed']==4096
closure = read(field/'field-erased-120/h1_final_verification.json')
assert closure['result'].startswith('PASS') and closure['waveform']=='NOT_VERIFIED'
hashes = {}
for folder in ('field-backup-120','field-programmed-120','field-erased-120'):
    p = field / folder
    files = sorted(f for f in p.iterdir() if f.suffix in ('.bin','.hex','.axf') or f.name=='backup.json')
    hashes[folder] = {f.name:sha(f) for f in files}
for name in ('field-program-01-local/program_result.json','field-erase-01-local/erase_result.json',
             'field-erased-120/h1_final_verification.json'):
    hashes[name]=sha(field/name)
save('h1_record_hashes.json', dict(source='local H1 original archives, verified without changing them',
    result=closure['result'], same_flash_identity_verified=True, pattern_crc32='0x6AFF4F17',
    bytes_programmed=520, bytes_erased=4096, final_target_and_neighbor_match_original=True,
    waveform='NOT_VERIFIED', nonblank_restore='NOT_RUN', exported_at=closure['exported_at'], sha256=hashes))
save('manifest.json', {p.relative_to(DEST).as_posix(): sha(p) for p in sorted(DEST.rglob('*'))
                        if p.is_file() and p.name != 'manifest.json'})
