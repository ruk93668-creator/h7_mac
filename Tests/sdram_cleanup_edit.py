from pathlib import Path
import hashlib,json,shutil,re,xml.etree.ElementTree as E
r=Path('.');t=r/'Tests/SdramCleanup';t.mkdir(exist_ok=True)
files=[p for d in ['CM7','CM4','Common','Board'] for p in (r/d).rglob('*') if p.is_file()]+list((r/'MDK-ARM').glob('*.sct'))+[r/'MDK-ARM/h7_mac.uvprojx',r/'MDK-ARM/h7_mac.uvoptx']
(t/'baseline.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},indent=2),encoding='utf-8')
for f in ['CM7/BSP/bsp_sdram.c','CM7/BSP/bsp_sdram.h','MDK-ARM/h7_mac.uvprojx','MDK-ARM/h7_mac.uvoptx']:
 dst=t/'before'/f;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(f,dst)
def read(f):return Path(f).read_bytes().decode('gbk').replace('\r\r\n','\n').replace('\r\n','\n')
def write(f,s):Path(f).write_bytes(s.replace('\n','\r\n').encode('gbk'))
f='CM7/BSP/bsp_sdram.c';s=read(f)
s=s.replace('volatile SysResult_t g_sdram_init_result = SYS_ERR_INVALID_STATE;\nvolatile uint32_t g_sdram_sdclk_hz;\n\n','')
s=s.replace('\tg_sdram_sdclk_hz = frequency / 2U;\n','')
s=s.replace('\tg_sdram_init_result = Sdram_Init();\n\treturn g_sdram_init_result;','\treturn Sdram_Init();')
write(f,s)
f='CM7/BSP/bsp_sdram.h';s=read(f);s=s.replace('//供调试器查看初始化结果和实际计算频率\nextern volatile SysResult_t g_sdram_init_result;\nextern volatile uint32_t g_sdram_sdclk_hz;\n\n','');write(f,s)
# Move only the two specifically authorized diagnostic files, checking both locations.
dst=(r/'Tests/SdramV0/diagnostics').resolve();dst.mkdir(exist_ok=True)
workspace=r.resolve()
for name in ['sdram_check.c','sdram_check.h']:
 src=(r/'CM7/Diagnostics'/name).resolve();target=dst/name
 assert src.is_relative_to(workspace/'CM7/Diagnostics') and target.is_relative_to(workspace/'Tests')
 assert not target.exists()
 shutil.move(str(src),str(target))
# Remove only diagnostics Group nodes and include path; preserve remaining XML text.
p=r/'MDK-ARM/h7_mac.uvprojx';raw=p.read_bytes().decode('utf-8')
pattern=r'\s*<Group>\s*<GroupName>CM7\\Diagnostics</GroupName>.*?</Group>'
s,n=re.subn(pattern,'',raw,flags=re.S);assert n==2,n
s=s.replace(';..\\CM7\\Diagnostics','');E.fromstring(s);p.write_bytes(s.encode('utf-8'))
p=r/'MDK-ARM/h7_mac.uvoptx';s=p.read_bytes().decode('utf-8')
s,n=re.subn(r'\s*<Group>\s*<GroupName>CM7\\Diagnostics</GroupName>.*?</Group>','',s,flags=re.S);assert n==1,n
# Remove stale watches only; preserve debugger and target configuration.
s=re.sub(r'\s*<Ww>\s*<count>[^<]*</count>\s*<WinNumber>[^<]*</WinNumber>\s*<ItemText>g_sdram_[^<]*</ItemText>.*?</Ww>','',s,flags=re.S)
# Handle alternate watch node structure using bounded nodes after inspecting actual schema.
E.fromstring(s);p.write_bytes(s.encode('utf-8'))
print('Removed diagnostics from formal targets; moved source to Tests/SdramV0/diagnostics; removed BSP diagnostic globals.')
