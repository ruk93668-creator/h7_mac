"""Generate isolated Keil validation targets; never flash automatically."""
from pathlib import Path
import subprocess,xml.etree.ElementTree as ET,copy,re,json,sys,hashlib,time
from e4_sources import PORT, LWIP, LWIP_DHCP, LWIP_TCP, TRANSPORT, BENCH, BOARD, HTTP, CLOCK, SERVICE
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'output/Ethernet';OUT.mkdir(parents=True,exist_ok=True)
baseline='--baseline' in sys.argv
smoke='--smoke' in sys.argv
e35='--e35' in sys.argv
e42='--e42' in sys.argv
e5='--e5' in sys.argv
e6='--e6' in sys.argv
e7_v0='--e7' in sys.argv
e7_v1_qvga='--e7-v1-qvga' in sys.argv
e7_v1_axi='--e7-v1-axi' in sys.argv or e7_v1_qvga
e7_v1='--e7-v1' in sys.argv or e7_v1_axi
e7=e7_v0 or e7_v1
tcp=e5 or e6 or e7
dhcp=e42 or tcp
if sum((baseline, smoke, e35, '--e4' in sys.argv, e42, e5, e6, e7_v0, '--e7-v1' in sys.argv, '--e7-v1-axi' in sys.argv, e7_v1_qvga)) > 1:
    raise SystemExit('choose only one validation variant')
e4='--e4' in sys.argv or dhcp
name='baseline' if baseline else 'e35' if e35 else 'e7_v1_qvga' if e7_v1_qvga else 'e7_v1_axi' if e7_v1_axi else 'e7_v1' if e7_v1 else 'e7' if e7_v0 else 'e6' if e6 else 'e5' if e5 else 'e42' if e42 else 'e4' if e4 else 'smoke' if smoke else 'integrated'
lwip_sources=LWIP + (LWIP_DHCP if dhcp else []) + (LWIP_TCP if tcp else [])
entry='e7' if e7 else name if tcp else 'e4'
bench_sources=PORT+CLOCK+['Tests/Ethernet/'+entry+'_main.c']
if tcp:
    bench_sources+=TRANSPORT+BOARD+(HTTP if e6 else SERVICE if e7 else BENCH)
camera_bsp = 'Tests/Ethernet/board_v1' if e7_v1 else 'CM7/BSP'
camera_sources = [f'{camera_bsp}/bsp_camera.c','CM7/BSP/bsp_dcmi.c','CM7/BSP/bsp_sdram.c',
                  'CM7/Device/Src/device.c','CM7/Device/Src/ov5640_device.c',
                  'CM7/Device/Components/OV5640/ov5640.c','CM7/Device/Components/OV5640/ov5640_reg.c',
                  'CM7/Media/Src/camera_capture.c','CM7/Media/Src/camera_mjpeg_fifo.c']
if e7:
    bench_sources += camera_sources + ['Tests/Ethernet/e7_camera_provider.c']
git=['git','-c','safe.directory='+ROOT.as_posix()]
project_bytes=subprocess.check_output(git+['show','b282c7c:MDK-ARM/h7_mac.uvprojx'],cwd=ROOT) if baseline else (ROOT/'MDK-ARM/h7_mac.uvprojx').read_bytes()
root=ET.fromstring(project_bytes);targets=root.find('Targets')
base=next(t for t in targets if t.findtext('TargetName')=='h7_mac_CM7')
for t in list(targets): targets.remove(t)
t=copy.deepcopy(base);targets.append(t)
# 独立目标有自己的协议配置和钩子
for group in list(t.find('Groups')):
    if group.findtext('GroupName') in {'Ethernet lwIP port', 'Ethernet Service', 'Ethernet lwIP 2.2.1'}:
        t.find('Groups').remove(group)
define=t.find('./TargetOption/TargetArmAds/Cads/VariousControls/Define')
define.text=','.join(value for value in (define.text or '').split(',')
                     if value not in {'ETH_LWIP_DHCP', 'ETH_LWIP_E5', 'ETH_NETWORK_SERVICE'})
link_misc=t.find('.//LDads/Misc')
link_misc.text='\n'.join(value for value in (link_misc.text or '').splitlines()
                          if not value.startswith(('--keep=eth_network_', '--keep=eth_http_', '--keep=eth_transport_', '--keep=eth_udp_')))
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
    # The production main group is removed below; keep the independent entry explicitly.
    file=ET.SubElement(files,'File')
    entry_source='e35_main.c' if e35 else 'test_main.c'
    ET.SubElement(file,'FileName').text=entry_source
    ET.SubElement(file,'FileType').text='1'
    ET.SubElement(file,'FilePath').text='../Tests/Ethernet/'+entry_source
    # Dedicated test entry; no Application code is changed or invoked.
    groups=t.find('Groups')
    keep={'Application/MDK-ARM','Drivers/STM32H7xx_HAL_Driver','Drivers/CMSIS',
          '::CMSIS','Ethernet independent driver','Ethernet RX verification only'}
    for group in list(groups):
        if group.findtext('GroupName') not in keep:
            groups.remove(group)
    # Remove link retention for services which are deliberately absent.
    t.find('.//LDads/Misc').text='--keep=*(.ethernet_dma)'

if not e4 and not baseline:
    # Raw-frame builds do not link the production lwIP CPU objects.
    scatter=OUT/(name+'.sct')
    scatter_text=(ROOT/'MDK-ARM/h7_mac_CM7_custom.sct').read_text()
    scatter_text, removed=re.subn(r'^    RW_ETHERNET_CPU [^\n]*\n    \{[^}]*\}\n\n',
                                  '', scatter_text, flags=re.M)
    if removed != 1:
        raise SystemExit('unexpected production Ethernet CPU region')
    scatter_text=re.sub(r'^    RW_NETWORK_JPEG [^\n]*\n    \{[^}]*\}\n\n',
                        '', scatter_text, flags=re.M)
    scatter_text=re.sub(r'^    RW_ETH_MDIO_FAULT [^\n]*\n    \{[^}]*\}\n\n',
                        '', scatter_text, flags=re.M)
    scatter_text=scatter_text.replace('RW_IRAM1 0x20000000 0x0001FFC0',
                                      'RW_IRAM1 0x20000000 0x00020000')
    if smoke or e35:
        scatter_text=re.sub(r'(?ms)^[ \t]*(?:ER_FW_INIT|ER_DEVICE_EXPORT|RW_SHARED|RW_SDMMC_DMA)[^{]*\{[^}]*\}[ \t]*\r?\n',
                            '', scatter_text)
    if e35:
        scatter_text=scatter_text.replace('    RW_IRAM1', '    RW_E35_CACHE 0x24010000 0x00010000\n    {\n        *(.e35_cache)\n    }\n\n    RW_IRAM1')
    scatter.write_text(scatter_text)
    t.find('.//LDads/ScatterFile').text=str(scatter)
if e4:
    # Use only CM7 startup, the existing Ethernet driver and the required HAL files.
    groups=t.find('Groups')
    keep={'Application/MDK-ARM','Drivers/STM32H7xx_HAL_Driver','Drivers/CMSIS',
          '::CMSIS','Ethernet independent driver'}
    for group in list(groups):
        if group.findtext('GroupName') not in keep:
            groups.remove(group)
        elif group.findtext('GroupName')=='Drivers/STM32H7xx_HAL_Driver':
            hal={'hal','hal_cortex','hal_pwr','hal_pwr_ex','hal_rcc','hal_rcc_ex',
                 'hal_flash','hal_flash_ex','hal_gpio'}
            allowed={'stm32h7xx_'+stem+'.c' for stem in hal}
            if e7:
                allowed.update('stm32h7xx_'+stem+'.c' for stem in
                               ('hal_dcmi','hal_dma','hal_dma_ex','hal_sdram','hal_mdma','ll_fmc'))
            if e7:
                # Mainline and the independent V1 camera BSP both use HAL I2C.
                allowed.update(('stm32h7xx_hal_i2c.c', 'stm32h7xx_hal_i2c_ex.c'))
            files=group.find('Files')
            for file in list(files):
                if file.findtext('FileName') not in allowed:
                    files.remove(file)
    controls=t.find('./TargetOption/TargetArmAds/Cads/VariousControls')
    controls.find('IncludePath').text=';'.join((
        '../Platform/Ethernet','../ThirdParty/lwip/src/include','../CM7/Core/Inc',
        '../CM7/Platform','../CM7/Service/Ethernet',
        '../Drivers/STM32H7xx_HAL_Driver/Inc','../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy',
        '../Drivers/CMSIS/Device/ST/STM32H7xx/Include','../Drivers/CMSIS/Include'))
    controls.find('MiscControls').text=''
    if e7:
        controls.find('IncludePath').text+=';'+';'.join('../'+p for p in (
            'CM7/BSP','CM7/Device/Inc','CM7/Device/Components/OV5640','CM7/Framework',
            'CM7/Media/Inc','CM7/Platform','Common/Inc','Board/Inc'))
        controls.find('MiscControls').text='--preinclude ../CM7/BSP/bsp_sdram_hal_config.h --preinclude ../CM7/BSP/bsp_dcmi_hal_config.h'
    if e7_v1:
        # Device-layer includes must resolve the same V1 header as its BSP source.
        include=controls.find('IncludePath')
        include.text='../'+camera_bsp+';'+include.text
    if dhcp:
        define=controls.find('Define')
        define.text=(define.text or '')+',ETH_LWIP_DHCP'+(',ETH_LWIP_E5' if tcp else '')
    if e7:
        controls.find('Define').text+=',ETH_MJPEG_FAST_SEND_TEST'
    if e7_v1_axi:
        define=controls.find('Define')
        define.text+=',ETH_E7_JPEG_AXI'
        if e7_v1_qvga:
            define.text+=',ETH_E7_CAMERA_QVGA'
    t.find('.//LDads/Misc').text='--keep=*(.ethernet_dma)'
    if e7:
        t.find('.//LDads/Misc').text+=' --keep=*(.device_export) --keep=*(.e7_jpeg)'
    scatter=ROOT/'Tests/Ethernet'/(entry+'.sct')
    if e7_v1_axi:
        layout=scatter.read_bytes()
        sdram=b'RW_E7_JPEG 0xD07F0000'
        if layout.count(sdram) != 1:
            raise SystemExit('unexpected E7 JPEG region layout')
        scatter=build_dir/(name+'.sct')
        scatter.write_bytes(layout.replace(sdram,b'RW_E7_JPEG 0x24020000'))
    t.find('.//LDads/ScatterFile').text=str(scatter)
    for label, paths in [('Ethernet port and bench', bench_sources),
                         ('lwIP 2.2.1', lwip_sources)]:
        group=ET.SubElement(groups,'Group')
        ET.SubElement(group,'GroupName').text=label
        files=ET.SubElement(group,'Files')
        for path in paths:
            file=ET.SubElement(files,'File')
            ET.SubElement(file,'FileName').text=Path(path).name
            ET.SubElement(file,'FileType').text='1'
            ET.SubElement(file,'FilePath').text='../'+path
# Give E3.5 its own uVision identity so an open smoke project cannot retain
# the old output directory when the generated target changes on disk.
project=ROOT/'MDK-ARM'/(('h7_mac_ethernet_'+name+'.uvprojx') if e4 else 'h7_mac_ethernet_e35.uvprojx' if e35 else 'h7_mac_ethernet_verify.uvprojx')
ET.ElementTree(root).write(project,encoding='utf-8',xml_declaration=True)
if '--prepare-only' in sys.argv: print(project);sys.exit(0)
log=OUT/(name+'_build.log')
cmd=['D:/32/UV4/UV4.exe','-j0','-r',str(project),'-t','h7_mac_CM7','-o',str(log)]
si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
inputs=list((ROOT/'Platform/Ethernet').glob('*.[ch]'))+list((ROOT/'Tests/Ethernet').glob('*.[ch]'))+[ROOT/'CM7/Core/Src/main.c',ROOT/'Tests/Ethernet/test_main.c',ROOT/'MDK-ARM/h7_mac.uvprojx',ROOT/'MDK-ARM/h7_mac_CM7_custom.sct']
inputs += list((ROOT/'Tests/Ethernet').glob('*.py'))
if e4:
    inputs += [scatter]
    inputs += list((ROOT/'Platform/Ethernet/arch').glob('*.h'))
    inputs += list((ROOT/'ThirdParty/lwip/src/include').rglob('*.h'))
    inputs += [ROOT/path for path in lwip_sources + CLOCK]
    inputs += [ROOT/path for path in SERVICE] if e6 or e7 else []
    if e6 or e7:
        inputs += list((ROOT/'CM7/Service/Ethernet').glob('*.h'))
if e6:
    inputs += sorted((ROOT/'Tests/Ethernet/e6_jpeg_fixtures').glob('*'))
if e7:
    inputs += [ROOT/path for path in camera_sources]
    inputs += [ROOT/camera_bsp/'bsp_camera.h']
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
    if e4:
        symbols=('s_network','ram_heap','memp_memory_PBUF_POOL_base')
        if dhcp:
            symbols+=('memp_memory_UDP_PCB_base','memp_memory_SYS_TIMEOUT_base')
        if tcp:
            symbols+=('memp_memory_TCP_PCB_base','memp_memory_TCP_PCB_LISTEN_base',
                      'memp_memory_TCP_SEG_base','s_connections','s_receive_data')
            symbols+=('s_http',) if e6 or e7 else ('s_channels',)
        addresses={symbol:re.search(r'^\s*'+symbol+r'\s+(0x[\da-f]+)\s+Data\b'+
                    (r'[^\n]*\s'+entry+r'_main\.o\(' if symbol == 's_network' else ''),
                    map_text,re.M|re.I) for symbol in symbols}
        result['e4_cpu_memory']={symbol:int(match[1],16) if match else None
                                 for symbol,match in addresses.items()}
        cache=re.search(r'Execution Region RW_E4_CACHE \(Exec base: 0x24010000.*Size: (0x[\da-f]+), Max: 0x00010000',
                        map_text, re.I)
        result['e4_cpu_link_valid']=bool(cache and
            all(address is not None and 0x24010000 <= address < 0x24020000
                for address in result['e4_cpu_memory'].values()) and
            result['e4_cpu_memory']['s_network'] % 32 == 0)
        if tcp:
            result[name+'_cache_used']=int(cache[1],16) if cache else None
            result[name+'_cache_free']=0x10000-int(cache[1],16) if cache else None
            result['e4_cpu_link_valid'] &= bool(cache and result[name+'_cache_free'] >= 8192)
        if e6:
            jpeg=re.findall(r'^\s*(e6_jpeg_[0-7])\s+(0x[\da-f]+)\s+Data\s+(\d+)',map_text,re.M|re.I)
            result['e6_jpeg_symbols']={symbol:{'address':addr,'bytes':int(size)} for symbol,addr,size in jpeg}
            result['e4_cpu_link_valid'] &= len(result['e6_jpeg_symbols']) == 8 and all(
                0x08000000 <= int(item['address'],16) < 0x08100000 for item in result['e6_jpeg_symbols'].values())
        if e7:
            jpeg_base='0x24020000' if e7_v1_axi else '0xd07f0000'
            jpeg_check='e7_axi_valid' if e7_v1_axi else 'e7_sdram_valid'
            result[jpeg_check]=bool(re.search(r'g_e7_jpeg\s+'+jpeg_base+r'\s+Data\s+61440',map_text,re.I) and
                re.search(r'Execution Region RW_E7_JPEG \(Exec base: '+jpeg_base+r'[^\n]*UNINIT\)',map_text,re.I) and
                re.search(jpeg_base+r'\s+-\s+0x0000f000\s+Zero\s+RW[^\n]*\.e7_jpeg',map_text,re.I) and
                re.search(r'Execution Region ER_DEVICE_EXPORT .*Size: 0x00000004',map_text,re.I))
            result['e4_cpu_link_valid'] &= result[jpeg_check]
            result['camera_board_revision']='V1'
            result['camera_bsp']=camera_bsp
            result['camera_resolution']=[320,240] if e7_v1_qvga else [160,120]
            if e7:
                result['e7_v1_i2c_link_valid']=all(re.search(
                    r'^\s*'+symbol+r'\s+0x[\da-f]+\s+Thumb Code[^\n]*stm32h7xx_hal_i2c\.o',
                    map_text, re.M|re.I)
                    for symbol in ('HAL_I2C_Init', 'HAL_I2C_Master_Transmit', 'HAL_I2C_Master_Receive'))
                result['e4_cpu_link_valid'] &= result['e7_v1_i2c_link_valid']
(OUT/(name+'_result.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result),flush=True)
if not m or m.groups()!=('0','0') or r.returncode!=0 or not result['source_stable'] or (not baseline and not result['dma_link_valid']) or (e35 and not result['test_cache_link_valid']) or (e4 and not result['e4_cpu_link_valid']):
 print(text[-6000:]);sys.exit(1)
