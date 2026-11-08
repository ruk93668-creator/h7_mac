"""Compile-time write-test guard checks; never talks to a card."""
from pathlib import Path
import subprocess,json
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent;OUT=ROOT/'output/SdmmcStage08B';OUT.mkdir(parents=True,exist_ok=True)
src=OUT/'policy_header.c';src.write_text('#include "sd_block_probe.h"\nint policy_header(void) { return APP_SD_BLOCK_WRITE_TEST_ENABLED; }\n')
base=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-I',str(HERE),'-I',str(ROOT/'CM7/BSP'),'-I',str(ROOT/'Common/Inc')]
valid=['APP_SD_BLOCK_TEST_ENABLED=1','APP_SD_BLOCK_WRITE_TEST_ENABLED=1','SD_TEST_CID0=1U','SD_TEST_CID1=2U','SD_TEST_CID2=3U','SD_TEST_CID3=4U','SD_TEST_CAPACITY_BYTES=34359738368ULL','SD_TEST_WRITE_FIRST_LBA=100U','SD_TEST_WRITE_BLOCKS=8U']
cases=[('default_read_only',[],True,''),('write_requires_probe',['APP_SD_BLOCK_WRITE_TEST_ENABLED=1'],False,'write_test_requires_probe_enabled'),('write_requires_identity',valid[:2],False,'write_test_requires_explicit_card_identity'),('LBA0_rejected',[x.replace('FIRST_LBA=100U','FIRST_LBA=0U') for x in valid],False,'write_test_must_not_include_LBA0'),('short_region_rejected',[x.replace('BLOCKS=8U','BLOCKS=1U') for x in valid],False,'write_test_must_not_include_LBA0'),('authorized_mock_configuration',valid,True,''),('illegal_switch_rejected',['APP_SD_BLOCK_TEST_ENABLED=2'],False,'SD_block_test_switches_must_be_zero_or_one')]
stress=['APP_SD_BLOCK_TEST_ENABLED=1','APP_SD_BLOCK_STRESS_TEST_ENABLED=1']
cases += [('stress_valid',stress,True,''),('stress_requires_probe',['APP_SD_BLOCK_STRESS_TEST_ENABLED=1'],False,'stress_test_requires_probe'),('stress_rejects_write',valid+['APP_SD_BLOCK_STRESS_TEST_ENABLED=1'],False,'stress_test_requires_probe'),('stress_switch_invalid',['APP_SD_BLOCK_STRESS_TEST_ENABLED=2'],False,'SD_block_stress_switch'),('stress_zero_duration',stress+['SD_BLOCK_STRESS_DURATION_MS=0'],False,'stress_limits_out_of_range'),('stress_long_duration',stress+['SD_BLOCK_STRESS_DURATION_MS=300001'],False,'stress_limits_out_of_range'),('stress_zero_rounds',stress+['SD_BLOCK_STRESS_MAX_ROUNDS=0'],False,'stress_limits_out_of_range'),('stress_many_rounds',stress+['SD_BLOCK_STRESS_MAX_ROUNDS=15001'],False,'stress_limits_out_of_range')]
rows=[]
for name,defines,success,diagnostic in cases:
 cmd=base+['-D'+x for x in defines]+['-c',str(src),'-o',str(OUT/'policy_header.o')]
 p=subprocess.run(cmd,cwd=ROOT,capture_output=True);output=(p.stdout+p.stderr).decode(errors='replace')
 passed=(p.returncode==0)==success and (not diagnostic or diagnostic in output)
 rows.append(dict(name=name,command=subprocess.list2cmdline(cmd),expected_compile_success=success,exit_code=p.returncode,output=output,result='PASS' if passed else 'FAIL'))
summary=dict(total=len(rows),passed=sum(x['result']=='PASS' for x in rows),failed=sum(x['result']=='FAIL' for x in rows))
(HERE/'policy_results.json').write_text(json.dumps(dict(summary=summary,cases=rows),ensure_ascii=False,indent=2),encoding='utf-8');print(summary)
raise SystemExit(bool(summary['failed']))
