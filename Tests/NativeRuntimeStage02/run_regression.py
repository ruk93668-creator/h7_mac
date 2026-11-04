"""Run unchanged historical suites; preserve their versioned sources and old reports."""
from pathlib import Path
import subprocess,json,sys,os,hashlib
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
SUITES=[
 ('Streaming','Tests/UvcStreaming/run_tests.py','Tests/UvcStreaming/results.json',36),
 ('Payload','Tests/UvcPayload/run_tests.py','Tests/UvcPayload/results.json',124),
 ('EP1','Tests/UvcEp1Lifecycle/run_tests.py','Tests/UvcEp1Lifecycle/results.json',45),
 ('Probe','Tests/UvcProbeCommit/run_tests.py','Tests/UvcProbeCommit/results.json',89),
 ('Cancel','Tests/UvcProbeCommit/run_cancellation_tests.py','Tests/UvcProbeCommit/cancellation_results.json',39),
 ('Core','Tests/UsbCoreDecoupling/run_tests.py','Tests/UsbCoreDecoupling/results.json',204),
 ('EP0_IRQ','Tests/Ep0IrqRace/run_race.py','Tests/Ep0IrqRace/results.json',4),
 ('Lifecycle','Tests/UvcLifecycle/run_tests.py','Tests/UvcLifecycle/results.json',40),
 ('Descriptors','Tests/UsbUvcDescriptors/run_tests.py','Tests/UsbUvcDescriptors/results.json',40),
 ('Strings','Tests/UsbUvcDescriptors/run_string_tests.py','Tests/UsbUvcDescriptors/string_results.json',8),
 ('Contract','Tests/UvcUsbCleanup/run_tests.py','Tests/UvcUsbCleanup/results.json',14),
 ('CameraAppInit','Tests/CameraAppInit/run_tests.py','Tests/CameraAppInit/results.json',13),
 ('CameraContext','Tests/CameraContextRefactor/run_tests.py','Tests/CameraContextRefactor/results.json',72),
 ('CameraStream','Tests/CameraStreamState/run_tests.py','Tests/CameraStreamState/results.json',75),
 ('BspMedia','Tests/BspMediaContinuous/run_tests.py','Tests/BspMediaContinuous/results.json',108),
 ('DcmiReplace','Tests/DcmiReplaceTarget/run_tests.py','Tests/DcmiReplaceTarget/results.json',133),
 ('VideoConfig','Tests/VideoConfig/run_tests.py','Tests/VideoConfig/results.json',28),
 ('OldBridge','Tests/UvcBridge/run_tests.py','Tests/UvcBridge/results.json',35),
 ('Native01','Tests/NativeRuntimeStage01/run_tests.py','Tests/NativeRuntimeStage01/results.json',56),
 ('Native01Usb','Tests/NativeRuntimeStage01/run_usb_control.py','Tests/NativeRuntimeStage01/usb_results.json',15),
 ('LegacyP0','Tests/FreeRtosP0/run_kernel_tests.py','Tests/FreeRtosP0/results.json',11),
]
paths=subprocess.check_output(['git','ls-files','-z','--','Tests'],cwd=ROOT).decode().split('\0')
original={p:(ROOT/p).read_bytes() for p in paths if p and not p.startswith('Tests/NativeRuntimeStage02/')}
env=dict(os.environ,PYTHONIOENCODING='utf-8',PYTHONPATH=str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
rows=[]
try:
 for name,script,resultfile,expected in SUITES:
    p=ROOT/resultfile;p.unlink(missing_ok=True)
    result=subprocess.run([sys.executable,script],cwd=ROOT,env=env,capture_output=True)
    output=(result.stdout+result.stderr).decode('utf-8',errors='replace')
    (HERE/(name+'_console.log')).write_text(output,encoding='utf-8')
    row={'suite':name,'command':[sys.executable,script],'exit':result.returncode,'expected_historical':expected}
    if p.exists():
        raw=p.read_bytes();(HERE/(name+'_results.json')).write_bytes(raw);d=json.loads(raw.decode('utf-8'))
        if isinstance(d,list):
            passed=sum(bool(x.get('pass',x.get('passed',False))) for x in d)
            summary={'total':len(d),'passed':passed,'failed':len(d)-passed}
        else:
            summary=d.get('summary',{})
            if not summary and isinstance(d.get('pass'),int):summary={k:d[k] for k in ['total','pass','fail'] if k in d}
        row['summary']=summary
    else:
        row['not_executed']=expected;row['reason']=output[-2000:]
    rows.append(row)
    (HERE/'regression_summary.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    print(name,row.get('summary',{'not_executed':expected}),'exit',result.returncode,flush=True)
finally:
 changed_source=[]
 for p,raw in original.items():
    target=ROOT/p
    if target.suffix in ['.c','.h','.py','.sct'] and (not target.exists() or target.read_bytes()!=raw):changed_source.append(p)
    if not target.exists() or target.read_bytes()!=raw:target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
 (HERE/'historical_assertion_audit.json').write_text(json.dumps({'historical_source_mutations':changed_source,'restored_versioned_test_reports':True},indent=2),encoding='utf-8')
 if changed_source:raise RuntimeError('Historical source mutated by runner: '+str(changed_source))
