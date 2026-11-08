"""Audit actual images, upstream hashes, protected sources and preserved local settings."""
from pathlib import Path
import sys,subprocess,json,hashlib,xml.etree.ElementTree as E
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from elftools.elf.elffile import ELFFile
checks={}
up=ROOT/'Middlewares/Third_Party/FatFs';manifest=json.loads((up/'UPSTREAM.json').read_text(encoding='utf-8'))
for name,expected in manifest['files'].items():checks['upstream_'+name]=hashlib.sha256((up/name).read_bytes()).hexdigest()==expected
protected=['Drivers','Middlewares/ST','Middlewares/Third_Party/FreeRTOS','CM7/BSP','CM7/Core','CM7/Application','CM7/Device','CM7/Media','CM7/Service','CM7/Framework','CM4','Common']
checks['protected_sources_unchanged']=not subprocess.check_output(['git','diff','--name-only','74ceab6','--',*protected],cwd=ROOT).strip()
base=ROOT/'tmp/FatFsStage08C/baseline'
for name in ['MDK-ARM/h7_mac.uvoptx','Tests/UsbCoreDecoupling/commands.json','Tests/UsbCoreDecoupling/results.md']:
    checks['preserved_'+Path(name).name]=(ROOT/name).read_bytes()==(base/Path(name).name).read_bytes()

def normalized(data,remove=False):
    tree=E.fromstring(data)
    if remove:
        for target in tree.findall('./Targets/Target'):
            groups=target.find('Groups')
            for group in list(groups):
                if group.findtext('GroupName')=='FatFs':groups.remove(group);continue
                files=group.find('Files')
                if files is not None:
                    for f in list(files):
                        if f.findtext('FileName') in ['storage_fs.c','storage_fs.h','diskio.c','fatfs_diskio.h','ffconf.h']:files.remove(f)
            for inc in target.findall('.//IncludePath'):
                if inc.text:inc.text=inc.text.replace(';../Middlewares/Third_Party/FatFs','')
    for node in tree.iter():
        node.tail=None
        if node.text is not None and not node.text.strip():node.text=None
    return E.tostring(tree)
project=ROOT/'MDK-ARM/h7_mac.uvprojx'
checks['local_project_settings_preserved']=normalized(project.read_bytes(),True)==normalized((base/'h7_mac.uvprojx').read_bytes())
checks['no_normal_test_entries']=b'FatFsStage08C' not in project.read_bytes()

def symbols(path):
    with path.open('rb') as f:
        elf=ELFFile(f);return {x.name:(x['st_value'],x['st_size']) for x in elf.get_section_by_name('.symtab').iter_symbols()}
normal=ROOT/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf';sy=symbols(normal)
checks['normal_no_test_symbols']=not any(n in sy for n in ['g_fatfs_acceptance','Acceptance_Task','g_sd_block_test','g_sdmmc_test','s_stress_buffer','g_usb_iso_timing'])
checks['idma_buffer_unchanged']=sy.get('g_sdmmc_dma_buffer')==(0x24000000,32768)
image=ROOT/'output/FatFsStage08C/hardware/h7_mac_CM7.axf';hw=symbols(image)
checks['acceptance_task_linked']='Acceptance_Task' in hw and 'g_fatfs_acceptance' in hw
checks['acceptance_stack_dtcm']='s_accept_stack' in hw and 0x20000000<=hw['s_accept_stack'][0] and hw['s_accept_stack'][0]+hw['s_accept_stack'][1]<=0x20020000
for n in ['s_write','s_read']:
    a,size=hw.get(n,(0,0));checks['cpu_buffer_'+n]=0x24010000<=a and a+size<=0x24060000 and a%32==0 and size>=131109
checks['acceptance_project_restored']=json.loads((HERE/'hardware_build.json').read_text(encoding='utf-8'))['project_restored']
checks['no_format_functions']=not any(n in hw for n in ['f_mkfs','f_fdisk'])
report=dict(total=len(checks),passed=sum(checks.values()),checks=checks,normal_axf_sha256=hashlib.sha256(normal.read_bytes()).hexdigest(),acceptance_axf_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),acceptance_symbols={n:dict(address=hex(hw[n][0]),bytes=hw[n][1]) for n in ['g_fatfs_acceptance','s_accept_stack','s_write','s_read']},hardware_verified=False)
(HERE/'audit_results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(report);assert all(checks.values())
