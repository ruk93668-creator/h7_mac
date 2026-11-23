"""Freeze text evidence, scope checks and image bounds; never collect binaries."""
from pathlib import Path
import subprocess, json, hashlib, shutil, re
from elftools.elf.elffile import ELFFile
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1/q12'
DEST = ROOT / 'Tests/QspiFlashQ1/q12/evidence'
DEST.mkdir(parents=True, exist_ok=True)
BASE = '457bd068dc53cfb61347aff090670dc98064c557'
def save(rel, value):
    path = DEST / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2), encoding='utf-8')
for folder in ('mock', 'hal_link', 'regression'):
    for file in (OUT / folder).glob('*'):
        if file.suffix in ('.json', '.log'):
            target = DEST / folder / file.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(file, target)
for file in (ROOT / 'output/QspiFlashQ1/mock').glob('*'):
    if file.name in ('commands.json', 'results.json', 'run.log'):
        target = DEST / 'q11' / file.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, target)
resources = {}
for variant in ('baseline', 'current'):
    shutil.copyfile(OUT / (variant + '_build.json'), DEST / (variant + '_build.json'))
    rows = []
    for core in ('CM7', 'CM4'):
        target = 'h7_mac_' + core
        text = (OUT / variant / 'MDK-ARM' / target / (target + '.htm')).read_text(errors='replace')
        stacks = {}
        for symbol in ('Q1_Task', 'Q12_Scan', 'Q12_Compare', 'BSP_QSPI_Read', 'BSP_QSPI_ReadStatus'):
            match = re.search(re.escape(symbol) + r'</STRONG>.*?Stack size (\d+) bytes.*?Max Depth = (\d+)', text, re.S)
            if match: stacks[symbol] = dict(frame=int(match[1]), static_chain=int(match[2]))
        maximum = re.search(r'Maximum Stack Usage =\s*(\d+)', text)
        map_text = (OUT / variant / 'MDK-ARM' / target / (target + '.map')).read_text(errors='replace')
        object_rows = [line for line in map_text.splitlines() if re.match(r'^\s*\d+\s+\d+\s+\d+\s+\d+\s+\d+\s+\d+\s+(bsp_qspi|m7_project_test)\.o\s*$', line)]
        image = OUT / variant / 'MDK-ARM' / target / (target + '.axf')
        with image.open('rb') as f:
            elf = ELFFile(f)
            loads = [dict(start=hex(s['p_paddr']), end_exclusive=hex(s['p_paddr'] + s['p_filesz']), bytes=s['p_filesz'])
                     for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD' and s['p_filesz']]
        rows.append(dict(target=target, maximum_static_stack=int(maximum[1]) if maximum else None,
                         stack_limitations='Unknown stack sizes/cycles/function pointers excluded; no board high-water measurement',
                         stacks=stacks, object_rows=object_rows, image_loads=loads,
                         image_sha256=hashlib.sha256(image.read_bytes()).hexdigest()))
        log_dest = DEST / variant / (target + '.log')
        log_dest.parent.mkdir(exist_ok=True)
        shutil.copyfile(OUT / variant / (target + '.log'), log_dest)
    resources[variant] = rows
save('resources.json', resources)
source = (ROOT / 'CM7/BSP/bsp_qspi.c').read_bytes()
old = subprocess.check_output(['git', 'show', BASE + ':CM7/BSP/bsp_qspi.c'], cwd=ROOT)
normalize = lambda b: b.replace(b'\r\n', b'\n').rstrip()
prefix = source.split('//按256B'.encode('utf-8'))[0]
assert normalize(prefix) == normalize(old), 'Q1.1 implementation changed'
for rel in ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h', 'Tests/QspiFlashQ1/hardware/m7_project_test.c', 'Tests/QspiFlashQ1/hardware/q12_scan.h']:
    assert (ROOT / rel).read_bytes() == (OUT / 'current' / rel).read_bytes(), 'Build/source mismatch: ' + rel
production = subprocess.check_output(['git', 'diff', BASE, '--name-only', '--', 'CM7', 'CM4', 'Common', 'Drivers', 'Board'], cwd=ROOT, text=True).splitlines()
assert sorted(production) == ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h']
assert not re.search(r'\b(malloc|calloc|realloc|free|pvPortMalloc|xTaskCreate|xSemaphoreCreate)\s*\(', source.decode('utf-8'))
save('scope.json', dict(baseline=BASE, production=production, q11_implementation_unchanged=True,
                       allowed_flash_opcodes=['9F', '05', '35', '15', '03', '0B', '6B'],
                       forbidden_commands='No WREN/program/erase/status-write/unprotect/Flash Reset; reviewed command construction and Mock allowlist',
                       hardware_executed=False,
                       local_keil_configs='Preserved and excluded from Git staging; project hash recorded in build JSON'))
files = {str(p.relative_to(DEST)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
         for p in DEST.rglob('*') if p.is_file() and p.name != 'manifest.json'}
save('manifest.json', files)
print('Text evidence files:', len(files))
