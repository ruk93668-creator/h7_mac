"""Build isolated formal project snapshots; never download or alter formal settings."""
from pathlib import Path
import subprocess, sys, tarfile, io, json, re, hashlib

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/QspiFlashQ1'
BASE = 'ce27b00062cba53ea90f137acc17ee360b8383ee'
OVERLAY = ['CM7/BSP/bsp_qspi.c', 'CM7/BSP/bsp_qspi.h',
           'CM7/Core/Inc/stm32h7xx_hal_conf.h', 'MDK-ARM/h7_mac.uvprojx']

def main():
    variant = sys.argv[1]
    assert variant in ('baseline', 'current')
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / '.gitignore').write_text('*\n')
    label = sys.argv[2] if len(sys.argv) > 2 else variant
    assert re.fullmatch(r'[A-Za-z0-9_-]+', label)
    dest = OUT / label
    assert not dest.exists(), 'Preserve previous evidence; choose a new run directory.'
    dest.mkdir()
    archive = subprocess.check_output(['git', 'archive', '--format=tar', BASE], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
        tf.extractall(dest)
    if variant == 'current':
        for rel in OVERLAY:
            (dest / rel).write_bytes((ROOT / rel).read_bytes())
    results = []
    for target in ['h7_mac_CM7', 'h7_mac_CM4']:
        log = dest / (target + '.log')
        cmd = ['D:/32/UV4/UV4.exe', '-j0', '-r', str(dest / 'MDK-ARM/h7_mac.uvprojx'),
               '-t', target, '-o', str(log)]
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        p = subprocess.run(cmd, cwd=dest, startupinfo=startup,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        raw = log.read_bytes() if log.exists() else b''
        txt = raw.decode('utf-8', errors='replace')
        counts = re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)', txt)
        size = re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', txt)
        row = dict(target=target, command=cmd, exit=p.returncode,
                   counts=list(map(int, counts.groups())) if counts else None,
                   size=list(map(int, size.groups())) if size else None,
                   log_sha256=hashlib.sha256(raw).hexdigest())
        row['pass'] = p.returncode == 0 and row['counts'] == [0, 0]
        results.append(row)
        print(row, flush=True)
        if not row['pass']:
            print(txt[-3000:], flush=True)
    (OUT / (label + '_build.json')).write_text(json.dumps(results, indent=2))
    return not all(r['pass'] for r in results)

if __name__ == '__main__':
    sys.exit(main())
