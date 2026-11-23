"""Rebuild the baseline and Q1.4, without touching the original Keil files."""
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
OUT = ROOT / 'output/QspiFlashQ1/q14'
BASE = '81d9b4b8b25598fc8193c3590cbb4551495c552a'
SOURCE = 'f480cc10485bf5939c13783f650887414d19516d'
variant = sys.argv[1]
assert variant in ('baseline', 'current')
directory = OUT / variant
directory.mkdir(parents=True)
rev = BASE if variant == 'baseline' else SOURCE
archive = subprocess.check_output(['git', 'archive', rev], cwd=ROOT)
with tarfile.open(fileobj=io.BytesIO(archive)) as files:
    files.extractall(directory, filter='data')

project_path = Path('MDK-ARM/h7_mac.uvprojx')
original = (ROOT / project_path).read_bytes()
macros = rb',QSPI_(?:SFDP_DIAGNOSTIC|QUAD_ID_DIAGNOSTIC|H1_BACKUP120|H1_BACKUP|H1_WRITE)(?:=[0-9]+)?(?=[,<])'
project, count = re.subn(macros, b'', original)
assert count <= 1
(directory / project_path).write_bytes(project)
sha = lambda data: hashlib.sha256(data).hexdigest()
rows = []
for target in ('h7_mac_CM7', 'h7_mac_CM4'):
    log = directory / (target + '.log')
    command = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(directory / project_path), '-t', target, '-o', str(log)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    code = subprocess.run(command, cwd=directory, startupinfo=startup,
                          creationflags=subprocess.CREATE_NO_WINDOW).returncode
    raw = log.read_bytes()
    assert code == 0 and re.findall(rb'(\d+) Error\(s\), (\d+) Warning\(s\)', raw)[-1:] == [(b'0', b'0')], log
    size = re.search(rb'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', raw)
    assert size
    folder = directory / 'MDK-ARM' / target
    axf = folder / (target + '.axf')
    with axf.open('rb') as f:
        elf = ELFFile(f)
        loads = [[s['p_paddr'], s['p_paddr'] + s['p_filesz']] for s in elf.iter_segments()
                 if s['p_type'] == 'PT_LOAD' and s['p_filesz']]
        names = {s.name for s in elf.get_section_by_name('.symtab').iter_symbols()}
    stack = re.search(r'Maximum Stack Usage =\s*(\d+)', (folder / (target + '.htm')).read_text(errors='replace'))
    assert stack
    row = dict(target=target, sizes=list(map(int, size.groups())), maximum_static_stack=int(stack[1]),
               errors=0, warnings=0, loads=loads, axf_sha256=sha(axf.read_bytes()), log_sha256=sha(raw),
               recover_linked='BSP_QSPI_Recover' in names)
    rows.append(row)
    print(json.dumps(row), flush=True)
(OUT / (variant + '_build.json')).write_text(json.dumps(dict(revision=rev, project_sha256=sha(project),
    source_sha256=sha((directory/'CM7/BSP/bsp_qspi.c').read_bytes()), builds=rows), indent=2) + '\n')
