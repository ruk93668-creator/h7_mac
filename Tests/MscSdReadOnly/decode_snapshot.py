"""Decode only the saved diagnostics; never read/write target hardware."""
from pathlib import Path
import sys,json
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'output/MSC4-readonly'
def read_hex(path):
 memory={};base=0
 for line in path.read_text().splitlines():
  if not line.startswith(':'):continue
  data=bytes.fromhex(line[1:]);assert sum(data)&255==0,'HEX checksum'
  count=data[0];address=int.from_bytes(data[1:3],'big');kind=data[3];payload=data[4:4+count]
  if kind==0:
   for i,value in enumerate(payload):memory[base+address+i]=value
  elif kind==4:base=int.from_bytes(payload,'big')<<16
  elif kind==2:base=int.from_bytes(payload,'big')<<4
 return memory
def decode(t,raw):
 if t['kind']=='struct':return {m['name']:decode(m['type'],raw[m['offset']:m['offset']+m['type']['size']]) for m in t['members']}
 if t['kind']=='array':
  size=t['element']['size'];return [decode(t['element'],raw[i*size:(i+1)*size]) for i in range(t['count'])]
 return int.from_bytes(raw,'little',signed=t.get('signed',False))
label=sys.argv[2] if len(sys.argv)>2 else 'init'
manifest=json.loads((OUT/'debug_manifest.json').read_text());results={};missing=[]
shared=read_hex(Path(sys.argv[1])) if len(sys.argv)>1 else None
for item in manifest['items']:
 path=Path(item['file'])
 if shared is None and not path.exists():missing.append(str(path));continue
 memory=shared if shared is not None else read_hex(path);raw=bytes(memory[item['address']+i] for i in range(item['type']['size']))
 value=decode(item['type'],raw)
 if item['unit']=='usb_msc_bot.c':
  value={k:v for k,v in value.items() if k not in ['data','cbw','csw']}
 results[item['unit']+':'+item['name']]=value
report=dict(elf_sha256=manifest['sha256'],diagnostics=results,missing=missing)
(OUT/('diagnostics/'+label+'_results.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
if missing:raise SystemExit(2)
sd=results['usb_msc_sd.c:s_sd_info'];worker=results['usb_msc_storage.c:s_storage'];diag=results['bsp_sdmmc.c:s_diagnostics'];io=results['bsp_sdmmc.c:s_io']
checks=dict(raw_claimed=results['usb_msc_sd.c:s_sd_claimed']==1,raw_client=results['sd_block.c:s_client']==2,raw_owner=results['sd_block.c:s_owner']!=0 and results['sd_block.c:s_owner']==results['storage_task.c:s_task'],not_locked=results['usb_msc_sd.c:s_sd_locked']==0,worker_initialized=worker['initialized']==1,media_ready=worker['media']['ready']==1,write_protected=worker['media']['write_protected']==1,sector_512=sd['logical_block_size']==512,capacity_nonzero=sd['logical_block_count']>16,init_success=diag['init_result']==0,transfer_mode=io['transfer_ready']==1,no_active_io=io['active']==0,no_fault=io['fault']==0,fatfs_no_lease=results['sd_block.c:s_client']==2 and results['storage_fs.c:s_diag']['state']!=2,memory_prepared=results['bsp_sdmmc_memory.c:s_memory_ready']==1)
report['checks']=checks;report['initialization_passed']=all(checks.values());(OUT/('diagnostics/'+label+'_results.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Initialization gate:',checks)
raise SystemExit(0 if all(checks.values()) else 1)
