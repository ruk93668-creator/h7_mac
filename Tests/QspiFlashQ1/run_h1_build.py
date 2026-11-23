"""Build two isolated H1 CM7 images with the original Keil target. No download."""
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[2]
BASE = 'ca841b78cfca4b5b50d20741e68a3af260f13259'
profile, run = sys.argv[1:]
assert profile in ('backup', 'backup120', 'write') and re.fullmatch(r'[A-Za-z0-9_-]+', run)
dest = ROOT / 'output/QspiFlashQ1/h1' / run / profile
dest.mkdir(parents=True)
archive = subprocess.check_output(['git', 'archive', BASE], cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(archive)) as files:
    files.extractall(dest, filter='data')
sources = ['CM7/BSP/bsp_qspi.c', 'Tests/QspiFlashQ1/hardware/m7_project_test.c',
           'Tests/QspiFlashQ1/hardware/qspi_h1.h', 'Tests/QspiFlashQ1/hardware/qspi_h1.inc']
sha = lambda data: hashlib.sha256(data).hexdigest()
for rel in sources:
    (dest / rel).write_bytes((ROOT / rel).read_bytes())

relative = 'MDK-ARM/h7_mac.uvprojx'
original = (ROOT / relative).read_bytes()
def select_macro(match):
    prior = rb',QSPI_(QUAD_ID_DIAGNOSTIC|SFDP_DIAGNOSTIC|H1_BACKUP120|H1_BACKUP|H1_WRITE)(?:=[0-9]+)?(?=,|$)'
    normal = re.sub(prior, b'', match[1])
    return b'<Define>' + normal + b',QSPI_H1_' + profile.upper().encode() + b'</Define>'

project, matches = re.subn(rb'<Define>(CORE_CM7,[^<]+)</Define>', select_macro, original)
assert matches == 1
(dest / relative).write_bytes(project)
target = 'h7_mac_CM7'
log = dest / (target + '.log')
command = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(dest / relative), '-t', target, '-o', str(log)]
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
code = subprocess.run(command, cwd=dest, startupinfo=startup,
                      creationflags=subprocess.CREATE_NO_WINDOW).returncode
raw = log.read_bytes()
assert code == 0 and re.findall(rb'(\d+) Error\(s\), (\d+) Warning\(s\)', raw)[-1:] == [(b'0', b'0')], log
axf = dest / 'MDK-ARM' / target / (target + '.axf')
with axf.open('rb') as f:
    elf = ELFFile(f)
    loads = [(p['p_paddr'], p['p_paddr'] + p['p_filesz']) for p in elf.iter_segments()
             if p['p_type'] == 'PT_LOAD' and p['p_filesz']]
    symbols = {s.name: (s['st_value'], s['st_size']) for s in elf.get_section_by_name('.symtab').iter_symbols()}
assert loads and all(0x08000000 <= a < b <= 0x08040000 for a, b in loads)
names = ['g_q1_hw_arm', 'g_q1_hw_report', 'g_qspi_h1_arm']
if profile in ('backup', 'backup120'):
    names += ['g_qspi_h1_capture']
    assert 'HAL_QSPI_Transmit' not in symbols and 'g_qspi_h1_input' not in symbols
else:
    names += ['g_qspi_h1_input', 'g_qspi_h1_report']
    assert 'HAL_QSPI_Transmit' in symbols and 'g_qspi_h1_capture' not in symbols
manifest = dict(profile=profile, baseline=BASE, compiler_exit=code, errors=0, warnings=0,
    data_range=[0x00FFF000, 0x01000000], loads=loads, axf_sha256=sha(axf.read_bytes()),
    axf=str(axf.relative_to(dest)), log_sha256=sha(raw), original_project_sha256=sha(original),
    project_sha256=sha(project), symbols={n: dict(address=symbols[n][0], size=symbols[n][1]) for n in names},
    sources={n: sha((dest / n).read_bytes()) for n in sources})
(dest / 'build.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(json.dumps(manifest, indent=2), flush=True)
