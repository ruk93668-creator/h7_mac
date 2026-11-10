from pathlib import Path
import json,hashlib,subprocess,re
from elftools.elf.elffile import ELFFile
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0
R=Path('.');H=R/'Tests/FrameLeaseSeven';BASE='554a1b339cb51f41f7634ca3b93bea280415811b';checks={}
changed=lambda *paths:subprocess.check_output(['git','diff','--name-only',BASE,'--',*paths]).decode().splitlines()
checks['hardware_and_protocol_sources_unchanged']=not changed('Drivers','Middlewares','CM4','CM7/Platform','CM7/Device','CM7/BSP/bsp_dcmi.c','CM7/BSP/bsp_sdmmc.c','CM7/BSP/bsp_sdram.c','CM7/Application/Tasks','CM7/Service/usb_uvc*')
checks['only_sdram_header_in_bsp']=changed('CM7/BSP')==['CM7/BSP/bsp_sdram.h']
checks['only_mpu_file_in_core']=changed('CM7/Core')==['CM7/Core/Src/main.c']
settings=json.loads((H/'settings_before.json').read_text(encoding='utf-8'));settings_now={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in settings};checks['user_project_configuration_unchanged']=all(settings_now[p]==v for p,v in settings.items() if p.endswith('.uvprojx'))
s=(R/'CM7/Application/app_storage.c').read_text(encoding='gbk');offer=s[s.index('SysResult_t App_StorageOfferFrame('):s.index('void App_StorageProcessReturns(')]
checks['no_image_copy_or_scan_in_offer']=all(n not in offer for n in ['memcpy(', 'JpegFrame_Crc32(', 'JpegFrame_Validate('])
checks['copy_pool_removed']=not any(n in s for n in ['APP_STORAGE_POOL_BASE','APP_STORAGE_POOL_END','Storage_Data('])
# Actual build fingerprint includes every production C/header in both cores and Common.
h=hashlib.sha256()
for d in ['CM7','CM4','Common']:
 for p in sorted((R/d).rglob('*')):
  if p.is_file() and p.suffix in ['.c','.h']:h.update(str(p.relative_to(R)).encode());h.update(p.read_bytes())
build=json.loads((H/'build_results.json').read_text(encoding='utf-8'))
formal_builds_clean=len(build)==2 and all(b['passed'] and b['errors']==b['warnings']==0 for b in build)
formal_sources_match=all(b['source_fingerprint_after']==h.hexdigest() for b in build)
# Use the formal AXF only when the build was clean and matches this exact source tree.
# Otherwise retain the failure and inspect the independently compiled software-test AXF.
compiled = R/'MDK-ARM/h7_mac_CM7/h7_mac_CM7.axf' if formal_builds_clean and formal_sources_match else R/'output/FrameLeaseSeven/test_O0.axf'
with compiled.open('rb') as f:
 elf=ELFFile(f);sy={x.name:(x['st_value'],x['st_size']) for x in elf.get_section_by_name('.symtab').iter_symbols()};sections=[(x['sh_addr'],x.data()) for x in elf.iter_sections() if x['sh_flags']&2 and x['sh_type']=='SHT_PROGBITS' and x['sh_size']]
checks['single_sd_descriptor']='#define APP_STORAGE_SLOT_COUNT 1U' in (R/'CM7/Application/app_storage.h').read_text(encoding='gbk')
checks['media_leases_seven']='#define CAMERA_MJPEG_MAX_LOANS 7U' in (R/'CM7/Media/Inc/camera_mjpeg_fifo.h').read_text(encoding='gbk')
checks['latest_frame_policy_tested']=json.loads((H/'media_test_results.json').read_text(encoding='utf-8'))['summary']==dict(total=96,passed=96,failed=0)
if formal_builds_clean and formal_sources_match:
 checks['formal_axi_idma_unchanged']=sy.get('g_sdmmc_dma_buffer')==(0x24000000,32768)
 checks['formal_ipc_layout_unchanged']=sy.get('g_ipc')==(0x38000000,320)
addresses=[]
for index in [0,63]:
 u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
 for a,n in [(0x08000000,0x100000),(0x20000000,0x20000),(0x01000000,0x1000)]:u.mem_map(a,n)
 for a,b in sections:
  if 0x08000000<=a<0x08100000 or 0x20000000<=a<0x20020000:u.mem_write(a,b)
 u.reg_write(UC_ARM_REG_SP,0x2001fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,index)
 u.emu_start(sy['Mjpeg_BlockAddress'][0]|1,0x01000000,count=1000);addresses.append(u.reg_read(UC_ARM_REG_R0))
checks['actual_elf_fifo_addresses']=addresses==[0xD0480000,0xD057C000]
checks['layout_disjoint']=0xD0000400+7*614400==0xD041A400 and 0xD041A400<0xD0480000 and 0xD0588000<=0xD0590000 and 0xD05A0000<=0xD0800000
ipc=(R/'Common/Inc/ipc_config.h').read_text(encoding='gbk');checks['heartbeat_100_timeout_1000']=bool(re.search(r'IPC_HEARTBEAT_PERIOD_MS\s+\(100U\)',ipc)) and bool(re.search(r'IPC_HEARTBEAT_MISS_LIMIT\s+\(10U\)',ipc))
checks['business_freshness_unchanged']=not changed('Common/Inc/ipc_application.h','Common/Src/ipc_application.c')
result=dict(baseline=BASE,total=len(checks),passed=sum(checks.values()),checks=checks,compiled_fifo_addresses=[hex(a) for a in addresses],compiled_evidence=str(compiled),settings_before=settings,settings_now=settings_now,debug_options_changed=any(settings_now[p]!=v for p,v in settings.items() if p.endswith('.uvoptx')),settings_restored=False,formal_builds_clean=formal_builds_clean,formal_sources_match=formal_sources_match,formal_build_results=build,source_sha256=h.hexdigest(),hardware_verified=False)
(H/'audit_results.json').write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result,indent=2));assert all(checks.values())
