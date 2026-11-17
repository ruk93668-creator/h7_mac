"""Generate isolated Keil validation targets; never flash automatically."""
from pathlib import Path
import subprocess,xml.etree.ElementTree as ET,copy,re,json,sys,hashlib,time
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/Ethernet';OUT.mkdir(parents=True,exist_ok=True)
baseline='--baseline' in sys.argv
smoke='--smoke' in sys.argv
e35='--e35' in sys.argv
if sum((baseline, smoke, e35)) > 1:
    raise SystemExit('choose only one validation variant')
name='baseline' if baseline else 'e35' if e35 else 'smoke' if smoke else 'integrated'
git=['git','-c','safe.directory='+ROOT.as_posix()]
project_bytes=subprocess.check_output(git+['show','b282c7c:MDK-ARM/h7_mac.uvprojx'],cwd=ROOT) if baseline else (ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()
root=ET.fromstring(project_bytes);targets=root.find('Targets')
base=next(t for t in targets if t.findtext('TargetName')=='h7_mac_CM7')
for t in list(targets): targets.remove(t)
t=copy.deepcopy(base);targets.append(t)
build_dir=OUT/name
build_dir.mkdir(parents=True,exist_ok=True)
# ARMCC response-file paths must not depend on uVision's inherited directory.
t.find('.//TargetCommonOption/OutputDirectory').text=str(build_dir)+'\\'
t.find('.//TargetCommonOption/ListingPath').text=str(build_dir)+'\\'
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
if smoke or e35:
    for f in t.findall('.//File'):
        if f.findtext('FilePath','').replace('\\','/').endswith('/Core/Src/main.c'):
            f.find('FilePath').text='../Tests/Ethernet/'+('e35_main.c' if e35 else 'test_main.c')
    group=ET.SubElement(t.find('Groups'),'Group')
    ET.SubElement(group,'GroupName').text='Ethernet RX verification only'
    files=ET.SubElement(group,'Files');file=ET.SubElement(files,'File')
    test_source='e35_frame.c' if e35 else 'eth_test_rx.c'
    ET.SubElement(file,'FileName').text=test_source
    ET.SubElement(file,'FileType').text='1'
    ET.SubElement(file,'FilePath').text='../Tests/Ethernet/'+test_source
    # Dedicated test entry; no Application code is changed or invoked.
if e35:
    # Test-only cached AXI SRAM for caller buffers/cache pressure. Production scatter unchanged.
    scatter=OUT/'e35.sct'
    scatter_text=(ROOT/'MDK-ARM/h7_mac_CM7_custom.sct').read_text()
    scatter_text=scatter_text.replace('    RW_IRAM1', '    RW_E35_CACHE 0x24010000 0x00010000\n    {\n        *(.e35_cache)\n    }\n\n    RW_IRAM1')
    scatter.write_text(scatter_text)
    t.find('.//LDads/ScatterFile').text=str(scatter)
project=ROOT/'MDK-ARM/h7_mac_ethernet_verify.uvprojx'
ET.ElementTree(root).write(project,encoding='utf-8',xml_declaration=True)
if '--prepare-only' in sys.argv: print(project);sys.exit(0)
log=OUT/(name+'_build.log')
cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t','h7_mac_CM7','-o',str(log)]
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
inputs=list((ROOT/'Platform/Ethernet').glob('*.[ch]'))+list((ROOT/'Tests/Ethernet').glob('*.[ch]'))+[ROOT/'CM7/Core/Src/main.c',ROOT/'Tests/Ethernet/test_main.c',ROOT/'MDK-ARM/h7_mac.uvprojx',ROOT/'MDK-ARM/h7_mac_CM7_custom.sct']
inputs += list((ROOT/'Tests/Ethernet').glob('*.py'))
def hashes():
    return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
before=hashes()
build_started=time.time()
rte=ROOT/'MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h'
rte_before=rte.read_bytes()
try:
    r=subprocess.run(cmd,cwd=project.parent,startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW,timeout=600)
finally:
    rte.write_bytes(rte_before)
text=log.read_text(errors='replace') if log.exists() else ''
m=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',text)
size=re.search(r'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)',text)
result={'variant':name,'exit':r.returncode,'errors':int(m[1]) if m else None,'warnings':int(m[2]) if m else None,'size':dict(zip(['code','ro','rw','zi'],map(int,size.groups()))) if size else None}
result['source_hashes']=before
result['source_stable']=before==hashes()
result['source_head']=subprocess.check_output(git+['rev-parse','HEAD'],cwd=ROOT,text=True).strip()
if not baseline:
    map_path=OUT/name/('eth_'+name+'.map')
    axf_path=OUT/name/('eth_'+name+'.axf')
    succeeded=r.returncode==0 and bool(m) and m.groups()==('0','0')
    fresh=succeeded and all(p.exists() and p.stat().st_mtime>=build_started-1 for p in (map_path,axf_path))
    result['artifacts_fresh']=bool(fresh)
    map_text=map_path.read_text(errors='replace') if fresh else ''
    result['dma_link_valid']=bool(fresh and re.search(r'g_eth_dma_memory\s+0x24078000\s+Data\s+12544',map_text))
    result['firmware_sha256']=hashlib.sha256(axf_path.read_bytes()).hexdigest() if fresh else None
    if e35:
        result['test_scatter_sha256']=hashlib.sha256(scatter.read_bytes()).hexdigest()
        result['test_cache_link_valid']=bool(fresh and re.search(r'Execution Region RW_E35_CACHE \(Exec base: 0x24010000.*Max: 0x00010000',map_text) and re.search(r's_tx\s+0x24010000\s+Data\s+1536',map_text) and re.search(r's_rx\s+0x24010600\s+Data\s+1536',map_text))
(OUT/(name+'_result.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result),flush=True)
if not m or m.groups()!=('0','0') or r.returncode!=0 or not result['source_stable'] or (not baseline and not result['dma_link_valid']) or (e35 and not result['test_cache_link_valid']):
 print(text[-6000:]);sys.exit(1)
