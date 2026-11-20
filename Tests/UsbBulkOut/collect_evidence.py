"""Collect C0 source/manual evidence and compare all software results; no production writes."""
from pathlib import Path
import subprocess,json,hashlib,pymupdf
R=Path(__file__).resolve().parents[2];O=R/'Tests/UsbBulkOut/reports';O.mkdir(exist_ok=True)
BASE='03a5cb1793e35ec23728f3a6ebbdc132277ed597'
def load(n):return json.loads((R/n).read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(n,v):(O/n).write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
assert subprocess.run(['git','merge-base','--is-ancestor',BASE,'HEAD'],cwd=R).returncode==0
protected=['CM7','CM4','Common','Board','Drivers','Middlewares','MDK-ARM']
assert subprocess.check_output(['git','diff','--name-only',BASE,'--',*protected],cwd=R).strip()==b''
# One Git tree read, then compare original file bytes using native Git blob IDs.
original_tests=subprocess.check_output(['git','ls-tree','-r','-z',BASE,'--','Tests'],cwd=R).split(b'\0')
for entry in original_tests:
 if not entry:continue
 meta,name=entry.split(b'\t',1);name=name.decode('utf-8');p=R/name
 if meta.split()[1]!=b'blob' or not p.is_file():continue
 data=p.read_bytes();oid=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
 assert oid==meta.split()[2].decode(),name
slices={'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pcd.c':[(1049,1180),(1332,1398),(1400,1480),(1992,2034),(2194,2331)],'Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_usb.c':[(915,963),(1004,1044)],'CM7/BSP/bsp_usb.c':[(478,538),(689,790),(992,1014),(1071,1103),(1180,1226)],'CM7/Platform/usb_device.c':[(605,617),(1031,1095),(1527,1544),(1600,1715)]}
sources=[]
for name,ranges in slices.items():
 p=R/name;assert p.read_bytes()==subprocess.check_output(['git','show',BASE+':'+name],cwd=R)
 lines=p.read_text(encoding='utf-8').splitlines();sources.append(dict(path=name,sha256=sha(p),slices=[dict(first=a,last=b,text='\n'.join(f'{j+1}: {lines[j]}' for j in range(a-1,b))) for a,b in ranges]))
pdf=R/'Docs/references/RM0399_Rev5.pdf';h=sha(pdf);assert h=='61d07b67deb291aed8a5cf8019c94add0a1065318542e57b3287d2694b14bd60';d=pymupdf.open(pdf)
manual=[dict(page=n,text=d[n-1].get_text()) for n in [2764,2775,2788,2789,2792,2793,2794,2795,2827,2828,2846,2847,2848,2849,2851,2852,2853,2903,2909,2910,2911]]
# Keep full manual page text in ignored output; Git evidence uses immutable page checksums/references.
full=R/'output/MSC24/udlm-p1c/c0-manual-fullpage-text.json';full.parent.mkdir(parents=True,exist_ok=True);full.write_text(json.dumps(manual,ensure_ascii=False,indent=2),encoding='utf-8')
refs=[dict(page=x['page'],text_sha256=hashlib.sha256(x['text'].encode('utf-8')).hexdigest()) for x in manual]
save('source_evidence.json',dict(baseline=BASE,sources=sources,manual=dict(revision='RM0399 Rev5',pages=len(d),sha256=h,page_references=refs,full_text_local_only=str(full.relative_to(R))),hardware='NOT_TESTED',production_changed=False))
ns={'__file__':str(R/'Tests/UsbBulkIn/compare_results.py')};exec((R/'Tests/UsbBulkIn/compare_results.py').read_text(encoding='utf-8').split('before=load(')[0],ns);cases=ns['cases']
before=load('Tests/UsbBulkIn/reports/regression-p1b1-final04.json');after=load('Tests/UsbBulkOut/reports/regression-p1c-c0.json');assert not after['original_changed'];by={x['suite']:x for x in before['rows']};rows=[]
for row in after['rows']:
 b=cases(by[row['suite']]);a=cases(row);detail=[]
 for k in sorted(set(a)|set(b)):
  x=b.get(k);y=a.get(k);detail.append(dict(case=k,baseline=x,current=y,verdict='MISSING' if y is None else 'NEW_CASE' if x is None else 'NEW_FAILURE' if x['passed'] and not y['passed'] else 'FIXED' if not x['passed'] and y['passed'] else 'UNCHANGED'))
 rows.append(dict(suite=row['suite'],baseline_exit=by[row['suite']]['exit'],current_exit=row['exit'],cases=detail))
ep='Tests/Ep0IrqRace/results.json';ep_old=json.loads(subprocess.check_output(['git','show',BASE+':'+ep],cwd=R).decode('utf-8'));ep_new=json.loads((Path(after['snapshot'])/ep).read_text(encoding='utf-8'));assert len(ep_new)==4 and all(x['pass'] for x in ep_new)
newfail=[dict(suite=r['suite'],case=x['case']) for r in rows for x in r['cases'] if x['verdict'] in ['NEW_FAILURE','MISSING']];assert not newfail
save('history_comparison.json',dict(baseline=BASE,suites=rows,ep0_unchanged_report_cases=[dict(baseline=next(x for x in ep_old if x['scenario']==n['scenario']),current=n) for n in ep_new],entries=dict(total=17,success=sum(x['exit']==0 for x in after['rows']),failed=sum(x['exit']!=0 for x in after['rows'])),new_failures=newfail))
b=load('Tests/UsbBulkOut/reports/in-baseline-p1c-c0.json');assert not b['original_changed'];in_results=[]
for row in b['rows']:
 assert row['exit']==0
 for item in row['produced_reports']:
  if item['path'].endswith(('/reports/specialist.json','/reports/integration.json')):
   summary=item['content']['summary'];assert summary['passed']==summary['total']==38;in_results.append(dict(suite=row['suite'],summary=summary,report_sha256=item['sha256']))
assert len(in_results)==2
build=load('Tests/UsbBulkOut/reports/build-p1c-c0.json');assert not build['original_changed'] and build['rows'][0]['exit']==0
snapshot=Path(build['snapshot']);builds=build['rows'][0]['produced_reports'][0]['content']['builds'];artifacts=[]
for x in sources:assert sha(snapshot/x['path'])==x['sha256']
for x in builds:
 assert x['summary']=='0 Error(s), 0 Warning(s)' and x['preprocessed_values']['WRITE']=='0'
 p=Path(x['artifact']);assert sha(p)==x['sha256'];m=p.with_suffix('.map');text=m.read_text(encoding='utf-8',errors='replace');assert 'USB_Device_GetStopProgress' in text and 'BSP_USB_ReconcileBulkInStop' in text
 artifacts.append(dict(mode=x['mode'],summary=x['summary'],defines=x['defines'],preprocessed=x['preprocessed_values'],files={str(f.relative_to(R)):sha(f) for f in p.parent.iterdir() if f.suffix.lower() in ['.axf','.hex','.map']}))
c0=load('Tests/UsbBulkOut/reports/hal_evidence.json');assert c0['summary']['passed']==c0['summary']['total']==24
for n,h in c0['sources'].items():assert sha(R/n)==h
assert not subprocess.check_output(['git','ls-files','--','Docs/references/RM0399_Rev5.pdf'],cwd=R)
save('verification_summary.json',dict(baseline=BASE,production_modified=False,hal_modified=False,in_baseline=in_results,c0=c0['summary'],historical_entries=17,historical_new_failures=newfail,builds=artifacts,pdf_sha256=sha(pdf),pdf_tracked=False,c1='UNRESOLVED: gate blocked, no production implementation',hardware='NOT_TESTED'))
print('VERIFIED: unchanged sources, manual evidence, 76 baseline cases, 24 C0 probes, 17 per-case comparisons, 3 full builds')
