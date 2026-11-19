"""Decode saved SRAM/registers offline; never connect to or alter the target."""
from pathlib import Path
import sys, json, ast, hashlib
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/MSC41'
mode,stage=sys.argv[1:3]
if mode not in ('uvc','ram','sd_readonly') or stage not in ('initial','reconnect'):
    raise SystemExit('Expected mode and initial/reconnect')
manifest=json.loads((OUT/(mode+'_manifest.json')).read_text(encoding='utf-8'))
if hashlib.sha256(Path(manifest['elf']).read_bytes()).hexdigest()!=manifest['sha256']:
    raise RuntimeError('ELF changed since manifest generation')
helpers={'Path':Path}
source=(ROOT/'Tests/MscSdReadOnly/decode_snapshot.py').read_text(encoding='utf-8')
for node in ast.parse(source).body:
    if isinstance(node,ast.FunctionDef) and node.name in ('read_hex','decode'):
        exec(ast.get_source_segment(source,node),helpers)
prefix='msc41_'+mode+'_'+stage
sram=ROOT/'MDK-ARM'/(prefix+'_sram.hex')
memory=helpers['read_hex'](sram)
values={}
for item in manifest['items']:
    address=item['address'];t=item['type']
    value=helpers['decode'](t,bytes(memory[address+i] for i in range(t['size'])))
    if item['name']=='s_bot':value={k:v for k,v in value.items() if k not in ('cbw','csw','data')}
    if item['name']=='s_device':value={k:v for k,v in value.items() if k in ('dev_state','dev_old_state','dev_config','dev_address','ep0_state','ep0_data_len','request','ep_in','ep_out')}
    values[item['unit']+':'+item['name']]=value
names=['START','STOP','DEINIT','RESET_BEGIN','RESET_END','CONNECT','DISCONNECT_BEGIN','DISCONNECT_END','SETUP_BEGIN','SETUP_END','EP0_IN','EP0_OUT','EP0_ERROR','ADDRESS','EP0_OPEN','EP0_CLOSE']
trace=values.pop('usb_device.c:s_lifecycle_diag')
entries=sorted((entry for entry in trace['entries'] if max(0,trace['sequence']-128)<entry['sequence']<=trace['sequence']),key=lambda e:e['sequence'])
for entry in entries:entry['event_name']=names[entry['event']-1] if 1<=entry['event']<=len(names) else 'UNKNOWN'
registers={};missing=[];files={str(sram.relative_to(ROOT)):hashlib.sha256(sram.read_bytes()).hexdigest()}
for part in ('gint','device','masks','empty_mask','in0','out0'):
    path=ROOT/'MDK-ARM'/(prefix+'_'+part+'.hex')
    if not path.exists():missing.append(str(path));continue
    registers.update(helpers['read_hex'](path));files[str(path.relative_to(ROOT))]=hashlib.sha256(path.read_bytes()).hexdigest()
words={}
for label,address in {'GINTSTS':0x40080014,'GINTMSK':0x40080018,'DCFG':0x40080800,'DCTL':0x40080804,'DSTS':0x40080808,'DIEPMSK':0x40080810,'DOEPMSK':0x40080814,'DAINT':0x40080818,'DAINTMSK':0x4008081c,'DIEPEMPMSK':0x40080834,'DIEPCTL0':0x40080900,'DIEPINT0':0x40080908,'DIEPTSIZ0':0x40080910,'DOEPCTL0':0x40080b00,'DOEPINT0':0x40080b08,'DOEPTSIZ0':0x40080b10}.items():
    if all(address+i in registers for i in range(4)):words[label]=int.from_bytes(bytes(registers[address+i] for i in range(4)),'little')
report=dict(mode=mode,stage=stage,elf_sha256=manifest['sha256'],files_sha256=files,diagnostics=values,trace_sequence=trace['sequence'],trace_exhausted=trace['exhausted'],trace_wrapped=trace['sequence']>128,events=entries,registers=words,missing_register_files=missing)
if 'DCFG' in words:report['hardware_device_address']=(words['DCFG']>>4)&127
(OUT/(prefix+'_results.json')).write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Saved',prefix,'events',len(entries),'hardware address',report.get('hardware_device_address','NOT_TESTED'),'missing registers',len(missing))
for entry in entries:print(entry['sequence'],entry['event_name'],hex(entry['detail']),'core',entry['device_state'],'ep0',entry['ep0_state'],'flags',hex(entry['ep0_flags']))
