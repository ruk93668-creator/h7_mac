from pathlib import Path
import json,subprocess,struct,sys,tempfile
sys.stdout.reconfigure(encoding='utf-8')
here=Path(__file__).resolve().parent;root=here.parents[1]
sys.path.insert(0,str(root/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
args=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-O0']
for d in ['Tests/CameraStreamState/stubs','Common/Inc','CM7/Media/Inc','CM7/BSP','CM7/Platform']:args+=['-I',str(root/d)]
args+=['-c',str(here/'layout_probe.c'),'-o',str(here/'layout_probe.o')]
p=subprocess.run(args,cwd=root,capture_output=True)
assert p.returncode==0,(p.stdout+p.stderr).decode(errors='replace')
with (here/'layout_probe.o').open('rb') as f:
 e=ELFFile(f);symbol=e.get_section_by_name('.symtab').get_symbol_by_name('g_layout')[0]
 section=e.get_section(symbol['st_shndx']);offset=symbol['st_value']-section['sh_addr']
 data=struct.unpack('<10I',section.data()[offset:offset+40])
base,capacity,window,end,start,stream_end,count,size,align,snapshot=data
rows=[]
def check(name,condition):
 rows.append({'name':name,'pass':bool(condition)});print(name,'PASS' if condition else 'FAIL')
check('FMC Bank2 base',base==0xD0000000)
check('Physical geometry capacity',capacity==(1<<13)*(1<<10)*4*2)
check('Business window exactly 128KiB',window==0x20000 and end==base+window)
check('Media end derives from same window',stream_end==end)
check('Reserved first 1KiB',start==base+1024)
check('MPU base alignment',base%window==0)
check('Three 40KiB stream buffers',count==3 and size==40960)
buffers=[(start+i*size,start+(i+1)*size) for i in range(count)]
for i,(a,b) in enumerate(buffers):
 check('Buffer %d aligned and in window'%i,a%align==0 and a%4==0 and base<=a<b<=end)
check('No stream buffer overlap',all(buffers[i][1]<=buffers[i+1][0] for i in range(count-1)))
check('Exact stream end',buffers[-1][1]==0xD001E400)
check('Buffer0 exact address',buffers[0][0]==0xD0000400)
check('Buffer1 exact address',buffers[1][0]==0xD000A400)
check('Buffer2 exact address',buffers[2][0]==0xD0014400)
check('Window tail 7KiB',end-buffers[-1][1]==7*1024)
check('Snapshot preserved and bounded',snapshot==63*1024 and start+snapshot<=end)
check('Window smaller than physical chip',window<=capacity)
# Compile the actual Media source with isolated invalid BSP headers.
header=(root/'CM7/BSP/bsp_sdram.h').read_bytes()
for label,value in [('64KiB window cannot fit stream',b'0x00010000UL'),
                    ('zero window rejected',b'0x00000000UL'),
                    ('non power-of-two window rejected',b'0x00030000UL'),
                    ('window beyond chip rejected',b'0x08000000UL')]:
 with tempfile.TemporaryDirectory() as scratch:
  temp=Path(scratch)
  (temp/'bsp_sdram.h').write_bytes(header.replace(b'0x00020000UL',value))
  negative=args[:5]+['-I',str(temp)]+args[5:]
  negative[-1]=str(temp/'rejected.o')
  probe=subprocess.run(negative,cwd=root,capture_output=True)
  output=(probe.stdout+probe.stderr).decode(errors='replace')
  check(label,probe.returncode!=0 and ('Invalid SDRAM business window' in output or 'Camera buffers must fit' in output))
result={'total':len(rows),'pass':sum(x['pass'] for x in rows),'fail':sum(not x['pass'] for x in rows),'compiled_values':list(data),'buffers':buffers,'command':subprocess.list2cmdline(args),'cases':rows}
(here/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
assert not result['fail']
