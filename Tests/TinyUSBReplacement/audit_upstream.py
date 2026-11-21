"""Verify pinned vendor bytes, enumerate the exact local compatibility delta and record sources."""
from pathlib import Path
import json,hashlib,difflib,subprocess
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent;V=ROOT/'ThirdParty/tinyusb';U=ROOT/'output/TinyUSBReplacement/upstream'
m=json.loads((V/'UPSTREAM.json').read_text(encoding='utf-8'));tree=json.loads((U/'tinyusb-tree.json').read_text(encoding='utf-8'));by={x['path']:x for x in tree['tree']};changes=[];rows=[];patches=[]
for name,sha in m['files'].items():
 data=(V/name).read_bytes();orig=(U/('tinyusb-'+m['sha'])/name).read_bytes()
 assert hashlib.sha256(orig).hexdigest()==sha
 blob=hashlib.sha1(b'blob '+str(len(orig)).encode()+b'\0'+orig).hexdigest();assert blob==by[name]['sha'],name
 if data!=orig:
  changes.append(name);patch=''.join(difflib.unified_diff(orig.decode().splitlines(True),data.decode().splitlines(True),fromfile='upstream/'+name,tofile='local/'+name));patches.append(patch)
 rows.append(dict(path=name,upstream_git_blob=blob,pristine_sha256=sha,current_sha256=hashlib.sha256(data).hexdigest()))
expected=['src/osal/osal_freertos.h','src/class/video/video.h','src/class/msc/msc_device.c','src/class/msc/msc_device.h','src/device/dcd.h','src/device/usbd.c','src/device/usbd.h','src/device/usbd_pvt.h','src/portable/synopsys/dwc2/dcd_dwc2.c','src/portable/synopsys/dwc2/dwc2_common.c','src/portable/synopsys/dwc2/dwc2_common.h']
assert set(changes)==set(expected),changes
(HERE/'reports/R2ResetClosure_upstream.patch').write_text(''.join(patches),encoding='utf-8')
facts=[]
for f in sorted(U.iterdir()):
 if f.is_file() and f.suffix!='zip' and not f.name.startswith('tinyusb-'):
  facts.append(dict(file=str(f.relative_to(ROOT)),sha256=hashlib.sha256(f.read_bytes()).hexdigest(),bytes=f.stat().st_size))
head=json.loads((U/'tinyusb-head.json').read_text(encoding='utf-8'));history=json.loads((U/'tinyusb-dwc2-history.json').read_text(encoding='utf-8'));issue=json.loads((U/'tinyusb-issue-3689.json').read_text(encoding='utf-8'))
report=dict(tinyusb_sha=m['sha'],tree_sha=tree['sha'],latest_queried_head=head['sha'],selected_files=rows,local_changes=changes,dcd_changes=[f for f in changes if '/dwc2/' in f],upstream_dwc2_history=[dict(sha=x['sha'],message=x['commit']['message']) for x in history],issue3689=dict(state=issue['state'],url=issue['html_url'],scope='ESP32-S2 supporting lead only, not H747 reproduction'),openmv_sha='da8dba466eb131691da776733e67b29021b12da6',openmv_micropython_submodule='57a7b25003de432e3910707fe7df51dd4c1b1326',micropython_head_also_reviewed='0fd6c573ea815774668bbb16b8e197c8822368b2',source_receipts=facts,hardware='NOT_TESTED')
prior=HERE/'reports/upstream_audit.json'
if prior.exists():(HERE/'reports'/('upstream-history-'+hashlib.sha256(prior.read_bytes()).hexdigest()[:12]+'.json')).write_bytes(prior.read_bytes())
(HERE/'reports/upstream_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print('verified pinned Git blobs',len(rows),'local delta',changes)
