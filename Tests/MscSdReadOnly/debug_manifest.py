"""Generate read-only debugger SAVE commands and type layouts from this exact ELF."""
from pathlib import Path
import sys,json,hashlib,struct
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'output/msc1-deps'))
from elftools.elf.elffile import ELFFile
OUT=ROOT/'output/MSC4-readonly';ELF=OUT/'sd_readonly/h7_mac_CM7_sd_readonly.axf';snap=OUT/'diagnostics/init';snap.mkdir(parents=True,exist_ok=True)
wanted={'usb_msc_bot.c':['s_bot'],'storage_task.c':['s_task'],'app_storage.c':['g_storage_status'],'usb_msc_sd.c':['s_sd_info','s_sd_cid','s_sd_claimed','s_sd_locked'],'usb_msc_storage.c':['s_storage'],'bsp_sdmmc.c':['s_diagnostics','s_io'],'bsp_sdmmc_memory.c':['s_memory_ready'],'sd_block.c':['s_owner','s_client'],'storage_fs.c':['s_diag','s_lease'],'usb_device.c':['s_last_result','s_started','s_initialized','s_stop_required','s_class_state','s_operation_result']}
def string(x):return x.decode(errors='replace') if isinstance(x,bytes) else str(x)
def unwrap(die):
 while die.tag in ('DW_TAG_typedef','DW_TAG_const_type','DW_TAG_volatile_type'):die=die.get_DIE_from_attribute('DW_AT_type')
 return die
def layout(die):
 die=unwrap(die);a=die.attributes
 if die.tag=='DW_TAG_pointer_type':return dict(kind='pointer',size=4)
 if die.tag=='DW_TAG_array_type':
  elem=layout(die.get_DIE_from_attribute('DW_AT_type'));n=1
  for child in die.iter_children():
   if child.tag=='DW_TAG_subrange_type':n*=child.attributes.get('DW_AT_count',child.attributes.get('DW_AT_upper_bound')).value+(0 if 'DW_AT_count'in child.attributes else 1)
  return dict(kind='array',size=elem['size']*n,count=n,element=elem)
 size=a['DW_AT_byte_size'].value
 if die.tag=='DW_TAG_structure_type':
  members=[]
  for child in die.iter_children():
   if child.tag!='DW_TAG_member':continue
   off=child.attributes['DW_AT_data_member_location'].value
   if not isinstance(off,int):
    assert off[0]==0x23
    value=0;shift=0
    for byte in off[1:]:
     value|=(byte&127)<<shift
     if not byte&128:break
     shift+=7
    off=value
   members.append(dict(name=string(child.attributes['DW_AT_name'].value),offset=off,type=layout(child.get_DIE_from_attribute('DW_AT_type'))))
  return dict(kind='struct',size=size,members=members)
 return dict(kind='scalar',size=size,signed=a.get('DW_AT_encoding').value in (5,6) if 'DW_AT_encoding'in a else False)
items=[];unavailable=[]
with ELF.open('rb') as f:
 elf=ELFFile(f);dwarf=elf.get_dwarf_info()
 for cu in dwarf.iter_CUs():
  unit=string(cu.get_top_DIE().attributes['DW_AT_name'].value).replace('\\','/').split('/')[-1]
  if unit not in wanted:continue
  for die in cu.iter_DIEs():
   if die.tag!='DW_TAG_variable' or 'DW_AT_name'not in die.attributes:continue
   name=string(die.attributes['DW_AT_name'].value)
   if name not in wanted[unit] or 'DW_AT_location'not in die.attributes:continue
   loc=die.attributes['DW_AT_location'].value
   if not isinstance(loc,list) or loc[0]!=3:continue
   address=int.from_bytes(bytes(loc[1:5]),'little')
   if address==0:
    unavailable.append(dict(unit=unit,name=name,reason='DWARF address zero; no allocated runtime location'));continue
   t=layout(die.get_DIE_from_attribute('DW_AT_type'))
   items.append(dict(unit=unit,name=name,address=address,type=t,file=str(snap/(unit.replace('.c','')+'_'+name+'.hex'))))
assert len(items)+len(unavailable)==sum(map(len,wanted.values())),[(i['unit'],i['name']) for i in items]
manifest=dict(elf=str(ELF),sha256=hashlib.sha256(ELF.read_bytes()).hexdigest(),items=items,unavailable=unavailable)
(OUT/'debug_manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
commands=['SAVE '+Path(item['file']).as_posix()+' '+hex(item['address'])+','+hex(item['address']+item['type']['size']-1) for item in items]
(OUT/'save_init.ini').write_text('\n'.join(commands)+'\n',encoding='ascii')
print('Prepared',len(items),'diagnostic-only memory ranges; total bytes',sum(x['type']['size'] for x in items))
