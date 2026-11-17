"""Check every MDC divider boundary through the real MAC initialization path."""
from pathlib import Path
import subprocess

exe = Path(__file__).resolve().parent / 'build/test_mac.exe'
cases = [(20000000, 512), (34999999, 512), (35000000, 768),
         (59999999, 768), (60000000, 0), (99999999, 0),
         (100000000, 256), (149999999, 256), (150000000, 1024),
         (249999999, 1024), (250000000, 1280), (300000000, 1280)]
for frequency, divider in cases:
    subprocess.run([str(exe), 'clock', str(frequency), str(divider)], check=True)
print('PASS: MDC divider boundaries (12 cases)')
