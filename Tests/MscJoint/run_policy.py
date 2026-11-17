"""Compile the real controlled-mode policy; reject unsafe macro combinations."""
from pathlib import Path
import subprocess,json
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'output/MSC24/policy';OUT.mkdir(parents=True,exist_ok=True)
source=OUT/'policy.c';source.write_text('#include "msc_test_config.h"\nint policy_probe(void) { return USB_MSC_SD_TEST; }\n')
cases=[([],None),(['USB_MODE_BOOT_MSC=1'],None),(['USB_MODE_BOOT_MSC=1','USB_MSC_SD_TEST=1'],None),(['USB_MODE_BOOT_MSC=1','USB_MSC_SD_TEST=1','USB_MSC_SD_WRITE_ENABLE=1'],None),(['USB_MSC_SD_TEST=1'],'SD_test_requires_explicit_MSC_boot_mode'),(['USB_MODE_BOOT_MSC=0','USB_MSC_SD_TEST=1'],'SD_test_requires_explicit_MSC_boot_mode'),(['USB_MSC_SD_WRITE_ENABLE=1'],'SD_write_requires_controlled_test_mode')]
rows=[]
for index,(defines,error) in enumerate(cases):
    command=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-I',str(ROOT/'CM7/Platform'),*['-D'+define for define in defines],'-c',str(source),'-o',str(OUT/(str(index)+'.o'))]
    result=subprocess.run(command,capture_output=True);output=(result.stdout+result.stderr).decode(errors='replace')
    passed=result.returncode==0 if error is None else result.returncode!=0 and error in output
    rows.append(dict(defines=defines,expected_error=error,exit=result.returncode,output=output,passed=passed))
(ROOT/'Tests/MscJoint/policy_results.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
print('policy cases:',len(rows),'passed:',sum(row['passed'] for row in rows))
assert all(row['passed'] for row in rows)
