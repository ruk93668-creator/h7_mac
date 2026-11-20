"""Offline preparation only. Never opens a board, captures live USB, resets or flashes."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys,shutil
ROOT=Path(__file__).resolve().parents[2]
OUTROOT=ROOT/'output/MSC24/udlm-p1-final'
def digest(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for chunk in iter(lambda:f.read(1048576),b''):h.update(chunk)
 return h.hexdigest()
def fresh_out(p):
 p=Path(p).resolve()
 if not p.is_relative_to(OUTROOT.resolve()) or p.exists():raise ValueError('output must be a new directory under '+str(OUTROOT))
 p.mkdir(parents=True);return p

def decode_ihex(path):
 memory={};upper=0;ended=False
 for line in Path(path).read_text(encoding='ascii').splitlines():
  if not line.strip():continue
  if ended or not line.startswith(':'):raise ValueError('invalid record placement')
  raw=bytes.fromhex(line[1:])
  if len(raw)<5 or len(raw)!=raw[0]+5 or sum(raw)&255:raise ValueError('checksum/length')
  n=raw[0];address=int.from_bytes(raw[1:3],'big');kind=raw[3];data=raw[4:4+n]
  if kind==0:
   for i,b in enumerate(data):
    a=upper+address+i
    if a in memory and memory[a]!=b:raise ValueError('conflicting overlap')
    memory[a]=b
  elif kind==1:
   if n!=0:raise ValueError('invalid EOF')
   ended=True
  elif kind in (2,4):
   if n!=2:raise ValueError('extended address length')
   upper=int.from_bytes(data,'big')*(16 if kind==2 else 65536)
  elif kind not in (3,5):raise ValueError('unsupported record')
 if not ended:raise ValueError('missing EOF')
 return memory

def capture_header(path):
 p=Path(path)
 with p.open('rb') as f:b=f.read(32)
 magic=b[:4]
 if magic in (bytes.fromhex(x) for x in ['d4c3b2a1','a1b2c3d4','4d3cb2a1','a1b23c4d']):
  if p.stat().st_size<24:raise ValueError('truncated pcap header')
  return 'pcap'
 if magic==bytes.fromhex('0a0d0d0a'):
  if len(b)<28:raise ValueError('truncated section header')
  endian='<' if b[8:12]==bytes.fromhex('4d3c2b1a') else '>' if b[8:12]==bytes.fromhex('1a2b3c4d') else None
  if endian is None:raise ValueError('bad byte order marker')
  length=struct.unpack(endian+'I',b[4:8])[0]
  if length<28 or length>p.stat().st_size or length%4:raise ValueError('section size')
  with p.open('rb') as f:f.seek(length-4);trailer=f.read(4)
  if struct.unpack(endian+'I',trailer)[0]!=length:raise ValueError('section trailer')
  return 'pcapng'
 raise ValueError('unknown capture magic')

def prepare(axf,out,mode,prefix):
 if not re.fullmatch(r'[A-Za-z0-9_]{1,40}',prefix):raise ValueError('unsafe command prefix')
 sys.path.insert(0,str(ROOT/'output/UDLM-P1/python_deps'))
 from elftools.elf.elffile import ELFFile
 axf=Path(axf).resolve();out=fresh_out(out);wanted=['s_pcd','s_stop_progress','s_bulk_in','s_out_slot','s_out_lease','s_bot','s_storage','s_uvc_payload','s_uvc_active','s_lifecycle_diag','s_sd_info','s_sd_cid','s_sd_claimed','s_usb_bridge'];found={n:set() for n in wanted}
 with axf.open('rb') as f:
  elf=ELFFile(f)
  for s in elf.get_section_by_name('.symtab').iter_symbols():
   if s.name in found and s['st_info']['type']=='STT_OBJECT' and s['st_size']>0:found[s.name].add((s['st_value'],s['st_size']))
 ranges=[(0x20000000,0x20020000),(0x24000000,0x24080000),(0x30000000,0x30048000),(0x38000000,0x38010000)]
 blocks=[];unresolved=[]
 for name,values in found.items():
  if len(values)!=1:unresolved.append(dict(symbol=name,reason='missing or ambiguous'));continue
  a,n=next(iter(values))
  if not any(a>=lo and a+n<=hi for lo,hi in ranges):unresolved.append(dict(symbol=name,reason='outside approved readable SRAM'));continue
  blocks.append(dict(name=name,address=a,size=n,file=prefix+'_'+name+'.hex'))
 # Curated, individually readable status/control registers only. Never GRXSTSP/DFIFO.
 for label,a in [('dbg_idcode',0x5c001000),('gsnpsid',0x40080040),('gintsts',0x40080014),('gintmsk',0x40080018),('dctl',0x40080804),('daintmsk',0x4008081c)]:blocks.append(dict(name=label,address=a,size=4,file=prefix+'_'+label+'.hex'))
 for ep in (0,1,2):
  for direction,offset in [('in',0x900),('out',0xb00)]:
   for label,off in [('ctl',0),('int',8),('size',16)]:blocks.append(dict(name=f'{direction}{ep}_{label}',address=0x40080000+offset+32*ep+off,size=4,file=f'{prefix}_{direction}{ep}_{label}.hex'))
 ini='\n'.join(f"SAVE {b['file']} 0x{b['address']:08X},0x{b['address']+b['size']-1:08X}" for b in blocks)+'\n';(out/'snapshot.ini').write_text(ini,encoding='ascii')
 manifest=dict(mode=mode,mode_verification='UNRESOLVED: operator label; verify macro/build receipt before any separately approved hardware execution',firmware=str(axf),firmware_sha256=digest(axf),hardware='NOT_TESTED',execution='requires separate approval; generation does not execute SAVE or halt CPU',blocks=blocks,unresolved_symbols=unresolved)
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8');return manifest

def snapshots(manifest,folder,out):
 m=json.loads(Path(manifest).read_text(encoding='utf-8'));rows=[]
 for b in m['blocks']:
  f=Path(folder)/b['file']
  if not f.exists():rows.append(dict(name=b['name'],status='NOT_TESTED',reason='missing file'));continue
  try:
   memory=decode_ihex(f);wanted=range(b['address'],b['address']+b['size'])
   if set(memory)!=set(wanted):raise ValueError('address set differs from exact approved block')
   data=bytes(memory[a] for a in wanted);rows.append(dict(name=b['name'],status='OBSERVED_RAW_ONLY',sha256=digest(f),address=b['address'],bytes=data.hex(),u32=[int.from_bytes(data[i:i+4],'little') for i in range(0,len(data)-3,4)]))
  except ValueError as e:rows.append(dict(name=b['name'],status='UNRESOLVED',reason=str(e)))
 dest=fresh_out(out);report=dict(rows=rows,hardware_acceptance='NOT_TESTED',proof_verdict='UNRESOLVED: raw words require schema and event/ticket correlation; Break perturbs timing');(dest/'snapshots.json').write_text(json.dumps(report,indent=2),encoding='utf-8');return report

def analyze_pcap(path,device_filter,out,tshark=None):
 if not device_filter.strip():raise ValueError('explicit device filter required')
 path=Path(path).resolve();fmt=capture_header(path);dest=fresh_out(out)
 exe=tshark or shutil.which('tshark') or 'C:/Program Files/Wireshark/tshark.exe'
 cmd=[exe,'-n','-r',str(path),'-Y',device_filter,'-T','json'];p=subprocess.run(cmd,capture_output=True)
 (dest/'tshark.stderr.log').write_bytes(p.stderr);(dest/'frames.json').write_bytes(p.stdout)
 report=dict(capture=str(path),sha256=digest(path),format=fmt,argv=cmd,exit=p.returncode,hardware_acceptance='NOT_TESTED',protocol_verdict='UNRESOLVED: review setup/tag/residue/ledger evidence, not counts alone')
 if p.returncode==0:
  packets=json.loads(p.stdout);fields={};requests={}
  def visit(node):
   if isinstance(node,dict):
    for k,v in node.items():
     fields[k]=fields.get(k,0)+1
     if k.endswith('.bRequest') and isinstance(v,str):requests[v]=requests.get(v,0)+1
     visit(v)
   elif isinstance(node,list):
    for v in node:visit(v)
  for packet in packets:visit(packet)
  report.update(frames=len(packets),field_inventory=fields,setup_request_observations=requests)
 (dest/'analysis.json').write_text(json.dumps(report,indent=2),encoding='utf-8');return report

def main():
 ap=argparse.ArgumentParser(description=__doc__);sub=ap.add_subparsers(dest='op',required=True)
 p=sub.add_parser('prepare');p.add_argument('--axf',required=True);p.add_argument('--out',required=True);p.add_argument('--mode',choices=['ram','sd_readonly','uvc'],required=True);p.add_argument('--prefix',required=True)
 p=sub.add_parser('snapshots');p.add_argument('--manifest',required=True);p.add_argument('--folder',required=True);p.add_argument('--out',required=True)
 p=sub.add_parser('pcap');p.add_argument('--capture',required=True);p.add_argument('--device-filter',required=True);p.add_argument('--out',required=True);p.add_argument('--tshark')
 a=ap.parse_args()
 if a.op=='prepare':r=prepare(a.axf,a.out,a.mode,a.prefix)
 elif a.op=='snapshots':r=snapshots(a.manifest,a.folder,a.out)
 else:r=analyze_pcap(a.capture,a.device_filter,a.out,a.tshark)
 print(json.dumps({k:v for k,v in r.items() if k not in ['blocks','rows','field_inventory']},indent=2))
if __name__=='__main__':main()
