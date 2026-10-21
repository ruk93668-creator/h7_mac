import argparse
from pathlib import Path
import json
import struct
from elftools.elf.elffile import ELFFile

parser = argparse.ArgumentParser()
parser.add_argument('image', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
with args.image.open('rb') as f:
    elf = ELFFile(f)
    symbols = {s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols()}
    export = symbols['g_device_export_s_oled']
    oled = symbols['s_oled']
    section = elf.get_section(export['st_shndx'])
    address = export['st_value']
    offset = address - section['sh_addr']
    raw = section.data()[offset:offset+4]
    pointer = struct.unpack('<I',raw)[0]
    flash = 0x08100000 <= address < 0x08200000
    matching_segment = next(s for s in elf.iter_segments() if s['p_type']=='PT_LOAD'
                            and s['p_vaddr'] <= address < s['p_vaddr']+s['p_filesz'])
    load_address = matching_segment['p_paddr'] + address - matching_segment['p_vaddr']
    assert section.name == 'ER_DEVICE_EXPORT'
    assert section['sh_size'] == 4 and export['st_size'] == 4
    assert flash and load_address == address
    # Device_t base is the first member of actual OledDevice_t.
    assert pointer == oled['st_value']
    assert 0x10000000 <= pointer < 0x10048000
    result = dict(scope='isolated CM4 link probe with test-only missing OLED operation stubs; not production Target',
                  image=str(args.image),section=section.name,export_address=f'0x{address:08X}',
                  load_address=f'0x{load_address:08X}',section_size=section['sh_size'],
                  stored_bytes=raw.hex(' '),stored_pointer=f'0x{pointer:08X}',
                  oled_base_address=f'0x{oled["st_value"]:08X}',flash=True,pointer_matches=True)
args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps(result,indent=2))
