"""No board or USB interface access: offline parser/preparation negative tests."""
from pathlib import Path
import json,time,struct,hashlib,shutil
import hardware_tools as h
root=h.OUTROOT/('offline-tools-'+str(time.time_ns()));root.mkdir(parents=True)
rows=[]
def check(name,fn):
 try:ok=bool(fn());error=None
 except Exception as e:ok=False;error=str(e)
 rows.append(dict(name=name,passed=ok,error=error));print(name,ok,error)
def rejects(fn):
 try:fn();return False
 except ValueError:return True
def rec(kind,address,data=b''):
 b=bytes([len(data),address>>8,address&255,kind])+data;return ':'+(b+bytes([-sum(b)&255])).hex().upper()+'\n'
valid=rec(4,0,bytes.fromhex('2000'))+rec(0,0,bytes.fromhex('78563412'))+rec(1,0)
p=root/'valid.hex';p.write_text(valid,encoding='ascii')
check('valid Intel HEX address and word',lambda:h.decode_ihex(p)=={0x20000000+i:b for i,b in enumerate(bytes.fromhex('78563412'))})
f=root/'bad.hex';f.write_text(valid.replace('78563412','78563413'),encoding='ascii');check('checksum failure rejected',lambda:rejects(lambda:h.decode_ihex(f)))
f2=root/'noeof.hex';f2.write_text(valid.rsplit(':',1)[0],encoding='ascii');check('missing EOF rejected',lambda:rejects(lambda:h.decode_ihex(f2)))
f3=root/'overlap.hex';f3.write_text(rec(0,0,b'A')+rec(0,0,b'B')+rec(1,0),encoding='ascii');check('conflicting overlap rejected',lambda:rejects(lambda:h.decode_ihex(f3)))
png=root/'header.pcapng';png.write_bytes(bytes.fromhex('0a0d0d0a')+struct.pack('<IIHHqI',28,0x1a2b3c4d,1,0,-1,28));check('pcapng section header validation only',lambda:h.capture_header(png)=='pcapng')
bad=root/'magic.pcapng';bad.write_bytes(b'\0'*28);check('zero magic rejected',lambda:rejects(lambda:h.capture_header(bad)))
short=root/'short.pcapng';short.write_bytes(png.read_bytes()[:12]);check('truncated section rejected',lambda:rejects(lambda:h.capture_header(short)))
check('output cannot overwrite workspace docs',lambda:rejects(lambda:h.fresh_out(h.ROOT/'Docs/should_not_exist')))
check('unsafe Keil prefix rejected before ELF reading',lambda:rejects(lambda:h.prepare('unused.axf',root/'unused','ram','x;RESET')))
m=root/'manifest.json';m.write_text(json.dumps(dict(blocks=[dict(name='x',address=0x20000000,size=4,file='missing.hex')])));check('missing snapshot is NOT_TESTED',lambda:h.snapshots(m,root,root/'missing-out')['rows'][0]['status']=='NOT_TESTED')
m.write_text(json.dumps(dict(blocks=[dict(name='x',address=0x24000000,size=4,file='valid.hex')])));check('wrong snapshot address rejected',lambda:h.snapshots(m,root,root/'wrong-out')['rows'][0]['status']=='UNRESOLVED')
exe=shutil.which('tshark') or 'C:/Program Files/Wireshark/tshark.exe'
if Path(exe).is_file():
 check('offline tshark empty capture never declares acceptance',lambda:(lambda r:r.get('frames')==0 and r['protocol_verdict'].startswith('UNRESOLVED'))(h.analyze_pcap(png,'usb.device_address == 2',root/'offline-pcap',exe)))
report=dict(cases=rows,summary=dict(cases=len(rows),passed=sum(x['passed'] for x in rows)),hardware='NOT_TESTED',sources={n:hashlib.sha256(Path(__file__).with_name(n).read_bytes()).hexdigest() for n in ['hardware_tools.py','run_hardware_tool_tests.py']},generated_inputs=str(root))
Path(__file__).with_name('reports').joinpath('offline_tools.json').write_text(json.dumps(report,indent=2));assert all(x['passed'] for x in rows)
