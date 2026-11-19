"""Copy existing FAT files to the PC only; never open SD sources for writing."""
from pathlib import Path
import json,hashlib,shutil,time,datetime
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'output/MSC4-readonly';inventory=json.loads((OUT/'file_inventory.json').read_text());source_root=Path(inventory['root']);copy_root=OUT/'copies';copy_root.mkdir(exist_ok=True)
identity=json.loads((OUT/'windows_identity.json').read_text(encoding='utf-8-sig'));assert len(identity['targets'])==1
assert len(identity['targets'][0]['volumes'])==1 and identity['targets'][0]['volumes'][0]['filesystem']=='FAT32'
assert source_root.drive.upper()==identity['targets'][0]['volumes'][0]['drive'].upper()
files=[f for f in inventory['files'] if f['size']>0];selected=[]
for name,low,high in [('small',0,65536),('medium',65536,4*1048576),('large',4*1048576,2**63)]:
 candidates=[f for f in files if low<f['size']<=high]
 if candidates:selected.append((name,max(candidates,key=lambda f:f['size'])))
required=sum(f['size'] for _,f in selected)
if shutil.disk_usage(OUT).free<required+8*1048576:raise RuntimeError('Insufficient PC space')
report=dict(source_root=str(source_root),original_reference='NOT_TESTED: no independent trusted original or SHA256 manifest supplied',files=[],missing_tiers=[n for n in ['small','medium','large'] if n not in [x[0] for x in selected]])
try:
 for tier,item in selected:
  source=source_root/item['relative'];destination=copy_root/tier/source.name;destination.parent.mkdir(exist_ok=True)
  if not source.resolve().is_relative_to(source_root.resolve()):raise RuntimeError('Source path escaped volume')
  if not destination.resolve().is_relative_to(copy_root.resolve()):raise RuntimeError('Destination escaped PC output')
  digest=hashlib.sha256();count=0;started=time.monotonic();next_progress=10*1048576
  with source.open('rb') as reader,destination.open('xb') as writer:
   while True:
    chunk=reader.read(1048576)
    if not chunk:break
    writer.write(chunk);digest.update(chunk);count+=len(chunk)
    (OUT/'copy_progress.json').write_text(json.dumps(dict(tier=tier,bytes=count,total=item['size'],elapsed_seconds=time.monotonic()-started)),encoding='utf-8')
    if count>=next_progress:print(tier,count,'/',item['size'],flush=True);next_progress+=10*1048576
  if count!=item['size']:raise RuntimeError('Source size changed or short read')
  target_hash=hashlib.file_digest(destination.open('rb'),'sha256').hexdigest()
  row=dict(tier=tier,source=item['relative'],bytes=count,destination=str(destination),stream_sha256=digest.hexdigest(),pc_sha256=target_hash,copy_consistent=target_hash==digest.hexdigest(),elapsed_seconds=time.monotonic()-started,independent_reference='NOT_TESTED')
  report['files'].append(row);assert row['copy_consistent'];print(tier,'PASS',count,target_hash,flush=True)
except Exception as error:
 report['error']=repr(error);raise
finally:(OUT/'file_copy_results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
