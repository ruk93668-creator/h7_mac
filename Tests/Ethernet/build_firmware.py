"""Generate isolated Keil validation targets; never flash automatically."""
from pathlib import Path
import subprocess,xml.etree.ElementTree as ET,copy,re,json,sys,hashlib
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/Ethernet';OUT.mkdir(parents=True,exist_ok=True)
baseline='--baseline' in sys.argv
smoke='--smoke' in sys.argv
name='baseline' if baseline else 'smoke' if smoke else 'integrated'
git=['git','-c','safe.directory='+ROOT.as_posix()]
project_bytes=subprocess.check_output(git+['show','b282c7c:MDK-ARM/h7_mac.uvprojx'],cwd=ROOT) if baseline else (ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()
root=ET.fromstring(project_bytes);targets=root.find('Targets')
base=next(t for t in targets if t.findtext('TargetName')=='h7_mac_CM7')
for t in list(targets): targets.remove(t)
t=copy.deepcopy(base);targets.append(t)
t.find('.//TargetCommonOption/OutputDirectory').text='..\\output\\Ethernet\\'+name+'\\'
t.find('.//TargetCommonOption/OutputName').text='eth_'+name
if baseline:
    scatter=OUT/'baseline.sct'
    scatter.write_bytes(subprocess.check_output(git+['show','b282c7c:MDK-ARM/h7_mac_CM7_custom.sct'],cwd=ROOT))
    t.find('.//LDads/ScatterFile').text=str(scatter)
    source=OUT/'baseline_main.c'
    source.write_bytes(subprocess.check_output(git+['show','b282c7c:CM7/Core/Src/main.c'],cwd=ROOT))
    for f in t.findall('.//File'):
        if f.findtext('FilePath','').replace('\\','/').endswith('/Core/Src/main.c'):
            f.find('FilePath').text=source.as_posix()
if smoke:
    for f in t.findall('.//File'):
        if f.findtext('FilePath','').replace('\\','/').endswith('/Core/Src/main.c'):
            f.find('FilePath').text='../Tests/Ethernet/test_main.c'
    # Dedicated test entry; no Application code is changed or invoked.
project=ROOT/'MDK-ARM/h7_mac_ethernet_verify.uvprojx'
ET.ElementTree(root).write(project,encoding='utf-8',xml_declaration=True)
if '--prepare-only' in sys.argv: print(project);sys.exit(0)
log=OUT/(name+'_build.log')
cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t','h7_mac_CM7','-o',str(log)]
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
inputs=list((ROOT/'Platform/Ethernet').glob('*.[ch]'))+[ROOT/'CM7/Core/Src/main.c',ROOT/'Tests/Ethernet/test_main.c',ROOT/'MDK-ARM/h7_mac.uvprojx',ROOT/'MDK-ARM/h7_mac_CM7_custom.sct']
def hashes():
    return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
before=hashes()
rte=ROOT/'MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h'
rte_before=rte.read_bytes()
try:
    r=subprocess.run(cmd,cwd=ROOT,startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW,timeout=600)
finally:
    rte.write_bytes(rte_before)
text=log.read_text(errors='replace') if log.exists() else ''
m=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
size=re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)',text)
result={'variant':name,'exit':r.returncode,'errors':int(m[1]) if m else None,'warnings':int(m[2]) if m else None,'size':dict(zip(['code','ro','rw','zi'],map(int,size.groups()))) if size else None}
result['source_hashes']=before
result['source_stable']=before==hashes()
if not baseline:
    map_path=OUT/name/('eth_'+name+'.map')
    map_text=map_path.read_text(errors='replace')
    result['dma_link_valid']=bool(re.search(r'g_eth_dma_memory\s+0x24078000\s+Data\s+12544',map_text))
(OUT/(name+'_result.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result),flush=True)
if not m or m.groups()!=('0','0') or r.returncode!=0 or not result['source_stable'] or (not baseline and not result['dma_link_valid']):
 print(text[-6000:]);sys.exit(1)
