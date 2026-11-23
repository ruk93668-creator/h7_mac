"""Rebuild frozen/current snapshots using the same local uvprojx; no download."""
from pathlib import Path
import subprocess, tarfile, io, json, re, hashlib, sys
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1/q12'
BASE = '457bd068dc53cfb61347aff090670dc98064c557'
variant = sys.argv[1]
assert variant in ('baseline', 'current')
dest = OUT / variant
assert not dest.exists(), 'Preserve evidence: use a new directory after an earlier build.'
dest.mkdir(parents=True)
with tarfile.open(fileobj=io.BytesIO(subprocess.check_output(['git', 'archive', BASE], cwd=ROOT))) as tf:
    tf.extractall(dest, filter='data')
#用户设置只作为只读构建输入，两侧逐字一致，绝不回写/提交用户工程配置。
project = ROOT / 'MDK-ARM/h7_mac.uvprojx'
(dest / 'MDK-ARM/h7_mac.uvprojx').write_bytes(project.read_bytes())
if variant == 'current':
    for rel in ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h',
                'Tests/QspiFlashQ1/hardware/m7_project_test.c',
                'Tests/QspiFlashQ1/hardware/q12_scan.h']:
        (dest / rel).write_bytes((ROOT / rel).read_bytes())
rows = []
for target in ('h7_mac_CM7', 'h7_mac_CM4'):
    log = dest / (target + '.log')
    command = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(dest / 'MDK-ARM/h7_mac.uvprojx'),
               '-t', target, '-o', str(log)]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    p = subprocess.run(command, cwd=dest, startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    txt = log.read_text(encoding='utf-8', errors='replace')
    counts = re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)', txt)
    size = re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', txt)
    row = dict(target=target, command=command, exit=p.returncode,
               counts=list(map(int, counts.groups())) if counts else None,
               size=list(map(int, size.groups())) if size else None,
               project_sha256=hashlib.sha256(project.read_bytes()).hexdigest())
    row['pass'] = row['exit'] == 0 and row['counts'] == [0, 0]
    rows.append(row)
    print(json.dumps(row), flush=True)
(OUT / (variant + '_build.json')).write_text(json.dumps(rows, indent=2))
sys.exit(not all(r['pass'] for r in rows))
