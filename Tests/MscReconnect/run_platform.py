"""Run unchanged deferred-status assertions with diagnostics off and on."""
from pathlib import Path
import json
ROOT = Path(__file__).resolve().parents[2]
original = ROOT / 'Tests/MscDeferredStatus/run_tests.py'
for mode in (0, 1):
    source = original.read_text(encoding='utf-8')
    source = source.replace('output/MSC1/ep0-deferred', 'output/MSC41/platform-' + str(mode))
    source = source.replace('Tests/MscDeferredStatus/results.json', 'Tests/MscReconnect/platform_' + str(mode) + '_results.json')
    source = source.replace("'--c99','-O0',", "'--c99','-O0','-DUSB_DEVICE_LIFECYCLE_DIAGNOSTICS=" + str(mode) + "',")
    exec(compile(source, str(original), 'exec'), {'__file__': str(original), '__name__': '__main__'})
