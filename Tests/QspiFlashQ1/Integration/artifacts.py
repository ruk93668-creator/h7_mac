"""Generate/check Intel HEX offline and report internal Flash scope, no download."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

root=Path(sys.argv[1]).resolve()
manifest=[]
for name in ('baseline_jpeg','baseline_raw','baseline_cm4','candidate_jpeg','candidate_raw','candidate_cm4','isolated_test'):
    folder=root/name
    target='h7_mac_CM4' if name.endswith('cm4') else 'h7_mac_CM7'
    axf=folder/(target+'.axf')
    hexfile=folder/(target+'.hex')
    if not hexfile.exists():
        subprocess.run(['D:/32/ARM/ARMCC/Bin/fromelf.exe','--i32','--output',str(hexfile),str(axf)],check=True)
    memory={};upper=0;eof=False
    for line in hexfile.read_text(encoding='ascii').splitlines():
        assert line.startswith(':') and not eof
        raw=bytes.fromhex(line[1:]);assert sum(raw)&255==0
        length=raw[0];addr=int.from_bytes(raw[1:3],'big');kind=raw[3];data=raw[4:-1]
        assert len(data)==length
        if kind==0:
            for i,value in enumerate(data):
                address=upper+addr+i
                assert address not in memory
                memory[address]=value
        elif kind==1:assert length==0;eof=True
        elif kind==4:assert length==2;upper=int.from_bytes(data,'big')<<16
        elif kind==2:assert length==2;upper=int.from_bytes(data,'big')<<4
        elif kind==5:assert length==4
        else:raise ValueError(kind)
    assert eof and memory
    low=min(memory);end=max(memory)+1
    core='CM4' if name.endswith('cm4') else 'CM7'
    bank=0x08100000 if core=='CM4' else 0x08000000
    assert bank<=low<end<=bank+0x100000
    # The ELF load bytes and independently parsed HEX must agree.
    from elftools.elf.elffile import ELFFile
    with axf.open('rb') as f:
        elf=ELFFile(f)
        expected={s['p_paddr']+i:v for s in elf.iter_segments() if s['p_type']=='PT_LOAD'
                  for i,v in enumerate(s.data())}
    assert memory==expected
    row=dict(variant=name,core=core,start=low,end_exclusive=end,data_bytes=len(memory),
             internal_sector_indices=sorted({(a-bank)//0x20000 for a in memory}),
             requires_actual_swap_bank_zero_confirmation=True,download_authorized=False,
             hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir()
                     if p.suffix in ('.axf','.hex','.map','.htm')},
             symbols=json.loads((folder/'result.json').read_text())['symbols'])
    manifest.append(row)
(root/'image_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest[-1],indent=2))
