from pathlib import Path
import hashlib,json,subprocess,xml.etree.ElementTree as E,struct,re
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
R=Path(__file__).resolve().parents[2];H=Path(__file__).resolve().parent
BASE='41a4ba4405d1a9eae5c84f9c9764432f3d052d76';checks={}
def baseline(name):return subprocess.check_output(['git','show',BASE+':'+name],cwd=R)
protected=['Drivers','CM4','CM7/BSP','CM7/Platform','CM7/Media','CM7/Device','CM7/Core','Middlewares','Common/Src','Common/Inc/ipc*','CM7/Application/Tasks/camera_task.c','CM7/Application/Tasks/usb_task.c','CM7/Application/Tasks/ipc_task.c','CM7/Application/app_events.c','CM7/Application/app_messages.c','CM7/Application/app_status.c']
checks['protected_sources_unchanged']=not subprocess.check_output(['git','diff','--name-only',BASE,'--',*protected],cwd=R).strip()
for name in ['MDK-ARM/h7_mac.uvoptx','MDK-ARM/h7_mac_CM7_custom.sct','MDK-ARM/h7_mac_CM4_custom.sct']:
 checks['unchanged_'+Path(name).name]=(R/name).read_bytes()==baseline(name)
def normalized(data,strip=False):
 root=E.fromstring(data)
 if strip:
  for t in root.findall('./Targets/Target'):
   gs=t.find('Groups')
   for g in list(gs):
    if g.findtext('GroupName')=='Stage08D Storage':gs.remove(g)
 for node in root.iter():
  node.tail=None
  if node.text is not None and not node.text.strip():node.text=None
 return E.tostring(root)
project=(R/'MDK-ARM/h7_mac.uvprojx').read_bytes()
checks['only_new_groups_in_keil']=normalized(project,True)==normalized(baseline('MDK-ARM/h7_mac.uvprojx'))
checks['no_historical_sd_test_defines']=b'APP_SD_BLOCK_TEST_ENABLED=1' not in project and b'APP_CAMERA_JPEG_TEST_ENABLED=1' not in project
old=baseline('CM7/Application/app_uvc_bridge.c').decode('gbk').replace('\r\n','\n');new=(R/'CM7/Application/app_uvc_bridge.c').read_bytes().decode('gbk').replace('\r\n','\n')
def function(s,name):
 start=s.index(name+'(');start=s.index('{',start);level=1;end=start+1
 while level:
  level+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
# Select definitions, not prototypes, for the private functions.
for name in ['App_UvcBridgeUsbProcess','Bridge_SameLease','Bridge_UsbReturn']:
 def body(s):
  marker=('void '+name if name=='App_UvcBridgeUsbProcess' else 'static bool '+name if name=='Bridge_SameLease' else 'static void '+name)
  begin=s.rindex(marker+'(');return function(s[begin:],name)
 checks['unchanged_'+name]=body(old)==body(new)
build=json.loads((H/'build_results.json').read_text(encoding='utf-8'));checks['full_builds_clean']=len(build)==2 and all(x['passed'] and not x['errors'] and not x['warnings'] for x in build)
fingerprint=hashlib.sha256()
for directory in ['CM7','CM4','Common']:
 for p in sorted((R/directory).rglob('*')):
  if p.is_file() and p.suffix in ['.c','.h']:
   fingerprint.update(str(p.relative_to(R)).encode());fingerprint.update(p.read_bytes())
checks['build_matches_final_sources']=all(x['source_fingerprint_after']==fingerprint.hexdigest() for x in build)
profiles=json.loads((H/'profile_builds.json').read_text(encoding='utf-8'));checks['profiles_clean']=len(profiles)==3 and all(not x['exit_code'] and not x['errors'] and not x['warnings'] and x['project_unchanged'] for x in profiles)
with (R/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf').open('rb') as f:
 elf=ELFFile(f);sy={x.name:(x['st_value'],x['st_size']) for x in elf.get_section_by_name('.symtab').iter_symbols()};sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
checks['storage_stack']=sy.get('s_stack',(0,0))[1]==8192 and sy['s_stack'][0]%8==0 and 0x20000000<=sy['s_stack'][0] and sum(sy['s_stack'])<=0x20020000
checks['original_idma']=sy.get('g_sdmmc_dma_buffer')==(0x24000000,32768)
checks['ipc_layout']=sy.get('g_ipc')==(0x38000000,320)
checks['five_boot_targets']=sy.get('s_tasks',(0,0))[1]==20
checks['no_legacy_test_symbols']=not any(n in sy for n in ['g_fatfs_acceptance','g_sd_block_test','g_camera_jpeg_test','g_sdmmc_test','g_usb_iso_timing'])
checks['storage_code_linked']=all(n in sy for n in ['StorageTask_Create','App_StorageProcess','App_StorageOfferFrame','MjpegAvi_Append','JpegFrame_Validate'])
addresses=[]
for slot in range(4):
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,size in [(0x08000000,0x100000),(0x20000000,0x20000),(0x01000000,0x1000)]:u.mem_map(a,size)
 for a,data in sections:
  if 0x08000000<=a<0x08100000 or 0x20000000<=a<0x20020000:u.mem_write(a,data)
 u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,slot);u.emu_start(sy['Storage_Data'][0]|1,0x01000000,count=1000);addresses.append(u.reg_read(UC_ARM_REG_R0))
checks['compiled_pool_addresses']=addresses==[0xD0384000+i*70016 for i in range(4)] and addresses[-1]+70016==0xD03C8600
checks['pool_and_index_within_mpu']=0xD03C8600<=0xD03D0000 and 0xD03D0000+4096*16<=0xD0400000
checks['single_state_writer']=len(re.findall(rb'\bs_state\s*=(?!=)',(R/'CM7/Application/app_storage.c').read_bytes()))==1
report=dict(baseline=BASE,total=len(checks),passed=sum(checks.values()),checks=checks,pool_addresses=[hex(a) for a in addresses],index_region=['0xd03d0000','0xd03e0000'],symbols={n:dict(address=hex(sy[n][0]),bytes=sy[n][1]) for n in ['s_stack','g_sdmmc_dma_buffer','g_ipc','g_storage_debug','s_tasks']},formal_axf_sha256=hashlib.sha256((R/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf').read_bytes()).hexdigest(),hardware_verified=False)
(H/'audit_results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(report);assert all(checks.values())
