"""Prepare read-only Keil snapshots for an exact diagnostic image. No breakpoints."""
from pathlib import Path
import ast, sys, json, hashlib
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'output/msc1-deps'))
from elftools.elf.elffile import ELFFile
mode = sys.argv[1]
if mode not in ('uvc', 'ram', 'sd_readonly'):
    raise SystemExit('Expected uvc / ram / sd_readonly')
OUT = ROOT/'output/MSC41'
ELF = OUT/'diagnostic-build'/mode/('h7_mac_CM7_'+mode+'.axf')
# Reuse only pure DWARF layout helpers; do not execute the historical script.
helpers = {}
text = (ROOT/'Tests/MscSdReadOnly/debug_manifest.py').read_text(encoding='utf-8')
for node in ast.parse(text).body:
    if isinstance(node, ast.FunctionDef) and node.name in ('string','unwrap','layout'):
        exec(ast.get_source_segment(text,node), helpers)
wanted = {
 'usb_device.c': ['s_device','s_endpoints','s_lifecycle_diag','s_control_token_seed','s_control_token','s_control_completed_token','s_deferred_status','s_stop_required','s_started','s_last_result','s_class_state'],
 'bsp_usb.c': ['s_pcd','s_tx_transfer','s_rx_active','s_ready','s_started'],
 'usb_msc_bot.c': ['s_bot'], 'usb_msc_storage.c': ['s_storage'],
 'bsp_sdmmc.c': ['s_io'], 'sd_block.c': ['s_owner','s_client'],
 'storage_fs.c': ['s_diag'], 'storage_task.c': ['s_task']}
items=[]
with ELF.open('rb') as stream:
 for cu in ELFFile(stream).get_dwarf_info().iter_CUs():
    unit=helpers['string'](cu.get_top_DIE().attributes['DW_AT_name'].value).replace('\\','/').split('/')[-1]
    if unit not in wanted: continue
    for die in cu.iter_DIEs():
        name=helpers['string'](die.attributes['DW_AT_name'].value) if 'DW_AT_name' in die.attributes else ''
        if die.tag!='DW_TAG_variable' or name not in wanted[unit] or 'DW_AT_location' not in die.attributes: continue
        loc=die.attributes['DW_AT_location'].value
        if not isinstance(loc,list) or loc[0]!=3: continue
        address=int.from_bytes(bytes(loc[1:5]),'little')
        if address: items.append(dict(unit=unit,name=name,address=address,type=helpers['layout'](die.get_DIE_from_attribute('DW_AT_type'))))
assert any(i['name']=='s_lifecycle_diag' for i in items), 'Diagnostic symbol missing'
assert all(0x20000000<=i['address'] and i['address']+i['type']['size']<=0x20020000 for i in items), 'Snapshot range insufficient'
manifest=dict(mode=mode,elf=str(ELF),sha256=hashlib.sha256(ELF.read_bytes()).hexdigest(),items=items)
(OUT/(mode+'_manifest.json')).write_text(json.dumps(manifest,indent=2),encoding='utf-8')
# Only safe status registers: never read GRXSTSP or FIFO data registers.
ranges={'gint':(0x40080014,0x4008001b),'device':(0x40080800,0x4008080b),'masks':(0x40080810,0x4008081f),'empty_mask':(0x40080834,0x40080837),'in0':(0x40080900,0x40080917),'out0':(0x40080b00,0x40080b17)}
for stage in ('initial','reconnect'):
    prefix='msc41_'+mode+'_'+stage
    lines=['SAVE '+prefix+'_sram.hex 0x20000000,0x2001FFFF']
    for name,(start,end) in ranges.items(): lines.append('SAVE '+prefix+'_'+name+'.hex '+hex(start)+','+hex(end))
    (ROOT/'MDK-ARM'/(prefix+'.ini')).write_text('\n'.join(lines)+'\n',encoding='ascii')
print(mode,manifest['sha256'],len(items),'diagnostic objects; read-only scripts prepared')
