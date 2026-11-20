"""Verify final source/build identity, real MAP references, and scope without modifying inputs."""
from pathlib import Path
import hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Tests/UsbBulkInIntegration/reports'
prod=['CM7/BSP/bsp_usb.c','CM7/BSP/bsp_usb.h','CM7/Platform/usb_device.c','CM7/Platform/usb_device.h','CM7/Service/usb_mode.c']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
d=json.loads((ROOT/'Tests/UsbBulkIn/reports/build-p1b1-final03.json').read_text());assert not d['original_changed'] and d['rows'][0]['exit']==0
snapshot=Path(d['snapshot']);sources={n:sha(ROOT/n) for n in prod}
for n in prod:assert sha(snapshot/n)==sources[n],n
builds=d['rows'][0]['produced_reports'][0]['content']['builds'];records=[]
pairs=[('UsbMode_AdvanceStop','USB_Device_Stop'),('UsbMode_AdvanceStop','USB_Device_GetStopProgress'),('Usb_StopDevice','Usb_DrainBulkInStops'),('Usb_DrainBulkInStops','USB_Device_ReconcileBulkInStop'),('USB_Device_ReconcileBulkInStop','BSP_USB_ReconcileBulkInStop'),('Usb_DrainBulkInStops','USB_Device_BeginBulkInStop'),('Usb_DrainBulkInStops','USB_Device_PollBulkInStop'),('Usb_DrainBulkInStops','USB_Device_CommitBulkInRetirement')]
for b in builds:
 assert b['summary']=='0 Error(s), 0 Warning(s)' and b['preprocessed_values']['WRITE']=='0'
 artifact=Path(b['artifact']);assert sha(artifact)==b['sha256'];m=artifact.with_suffix('.map');lines=m.read_text(encoding='utf-8',errors='replace').splitlines();refs=[]
 for a,z in pairs:
  matches=[dict(line=i+1,text=l.strip()) for i,l in enumerate(lines) if 'refers to' in l and '(i.'+a+')' in l and ' for '+z in l]
  assert matches,(b['mode'],a,z)
  refs.extend(matches)
 files={str(x.relative_to(ROOT)):sha(x) for x in artifact.parent.iterdir() if x.suffix.lower() in ['.axf','.hex','.map']}
 records.append(dict(mode=b['mode'],summary=b['summary'],defines=b['defines'],preprocessed=b['preprocessed_values'],map_references=refs,artifacts=files))
pdf=ROOT/'Docs/references/RM0399_Rev5.pdf';assert sha(pdf)=='61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60'
assert not subprocess.check_output(['git','ls-files','--','Docs/references/RM0399_Rev5.pdf'],cwd=ROOT)
changed=subprocess.check_output(['git','diff','--name-only'],cwd=ROOT).decode().splitlines()
for n in changed:assert n in prod or n.startswith('Docs/usb-lifecycle/') or n.startswith('Tests/UsbBulkIn/reports/'),n
cc=subprocess.run(['D:/32/ARM/ARMCC/Bin/armcc.exe','--vsn'],capture_output=True)
result=dict(baseline='5113000227e8d3d3a9a460b4585993e28096d0cc',compiler=(cc.stdout+cc.stderr).decode(errors='replace'),source_hashes=sources,builds=records,pdf_readonly_hash=sha(pdf),pdf_tracked=False,tracked_scope_valid=True,hardware='NOT_TESTED')
(OUT/'artifact_verification.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print('VERIFIED: three builds, eight real MAP edges each, final source hashes, PDF, scoped tracked diff')
