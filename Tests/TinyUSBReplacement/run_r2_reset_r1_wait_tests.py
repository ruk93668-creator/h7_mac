"""Run unchanged nine-case legacy assertions with fail-only unused new USBD adapters.
The standalone DCD wait image never linked the USBD Core. Full AXF success is tested separately.
"""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
p=HERE/'reports/r1b-waits-compile.json'
if p.exists():(HERE/'reports'/('r1b-waits-compile-history-'+hashlib.sha256(p.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(p.read_bytes())
s=(HERE/'run_r1b_wait_tests.py').read_text(encoding='utf-8')
s=s.replace("files=['Tests/TinyUSBReplacement/r1b_wait_test.c'", "files=['Tests/TinyUSBReplacement/r2_reset_r1_wait_adapter.c','Tests/TinyUSBReplacement/r1b_wait_test.c'",1)
# Add a separate stronger observation; preserve every original assertion and scenario.
s=s.replace('rows.append(row);print(row,flush=True)', "row['new_adapter_calls']=(struct.unpack('<I',u.mem_read(symbols['g_reset_r1_unused_hook_calls'],4))[0] if 'g_reset_r1_unused_hook_calls' in symbols else 0);row['adapter_linked']='g_reset_r1_unused_hook_calls' in symbols;rows.append(row);print(row,flush=True)",1)
try:exec(compile(s,str(HERE/'run_r1b_wait_tests.py'),'exec'),globals())
except SystemExit as e:
 if e.code:raise
if 'g_reset_r1_unused_hook_calls' not in symbols:
 assert all(n not in symbols for n in ['dcd_event_reset_try','dcd_event_setup_fence','dcd_event_setup_try']), 'counter absent with adapter code still linked'
assert all(r['new_adapter_calls']==0 for r in rows),'new Reset/SETUP adapters unexpectedly participated in legacy wait cases'
print('all nine original assertions passed; new adapter calls = 0')
