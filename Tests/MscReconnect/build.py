"""Build isolated RAM/RO/UVC diagnostic copies; never change the user's project or RTE bytes."""
from pathlib import Path
import subprocess,json,re,hashlib,shutil,shlex,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/MSC41/diagnostic-build';OUT.mkdir(parents=True,exist_ok=True)
PROJECT=ROOT/'MDK-ARM/h7_mac.uvprojx';RTE=ROOT/'MDK-ARM/RTE/_h7_mac_CM7/RTE_Components.h'
original=PROJECT.read_bytes();rte_original=RTE.read_bytes();options=ROOT/'MDK-ARM/h7_mac.uvoptx';options_original=options.read_bytes()
TREE=ET.fromstring(original);target=next(t for t in TREE.findall('./Targets/Target') if t.findtext('TargetName')=='h7_mac_CM7')
controls=target.find('TargetOption/TargetArmAds/Cads/VariousControls');base_defs=controls.findtext('Define') or ''
macros=['USB_MODE_BOOT_MSC','USB_MSC_SD_TEST','USB_MSC_SD_WRITE_ENABLE','USB_DEVICE_LIFECYCLE_DIAGNOSTICS']
for node in target.findall('.//Define'):
 if node is not controls.find('Define') and any(m in (node.text or '') for m in macros):raise RuntimeError('Per-file macro override requires review')
include=[str((ROOT/'MDK-ARM'/part).resolve()) for part in (controls.findtext('IncludePath') or '').split(';') if part]
rows=[];commands=[]
def run(args):
 result=subprocess.run(list(map(str,args)),cwd=ROOT/'MDK-ARM',capture_output=True);text=(result.stdout+result.stderr).decode(errors='replace');commands.append(dict(argv=list(map(str,args)),exit=result.returncode))
 if result.returncode:raise RuntimeError(text[-4000:])
 return text
for mode,extra in [('ram',['USB_MODE_BOOT_MSC=1','USB_MSC_SD_TEST=0','USB_MSC_SD_WRITE_ENABLE=0']),('sd_readonly',['USB_MODE_BOOT_MSC=1','USB_MSC_SD_TEST=1','USB_MSC_SD_WRITE_ENABLE=0']),('uvc',['USB_MODE_BOOT_MSC=0','USB_MSC_SD_TEST=0','USB_MSC_SD_WRITE_ENABLE=0'])]:
 dest=OUT/mode;dest.mkdir(exist_ok=True)
 defs=[d.strip() for d in base_defs.split(',') if d.strip() and d.strip().split('=')[0] not in macros]+extra+['USB_DEVICE_LIFECYCLE_DIAGNOSTICS=1']
 text=original.decode('utf-8')
 # Only isolated projects add sources absent from the user's retained project.
 service_group=next(g for g in target.findall('./Groups/Group') if g.findtext('GroupName')=='CM7\\Service')
 existing={f.findtext('FileName') for f in service_group.findall('./Files/File')}
 needed=['usb_msc_bot.c','usb_msc_scsi.c','usb_msc_ram.c','usb_msc_storage.c','usb_msc_sd.c']
 missing=[name for name in needed if name not in existing]
 group_start=text.index('<GroupName>CM7\\Service</GroupName>')
 files_end=text.index('</Files>',group_start)
 entries=''.join('<File><FileName>'+name+'</FileName><FileType>1</FileType><FilePath>../CM7/Service/'+name+'</FilePath></File>\n' for name in missing)
 text=text[:files_end]+entries+text[files_end:]
 at=text.index('<TargetName>h7_mac_CM7</TargetName>');head,tail=text[:at],text[at:]
 tail=re.sub(r'<Define>.*?</Define>','<Define>'+','.join(defs)+'</Define>',tail,count=1)
 tail=re.sub(r'<OutputDirectory>.*?</OutputDirectory>',lambda _: '<OutputDirectory>'+dest.as_posix()+'/</OutputDirectory>',tail,count=1)
 tail=re.sub(r'<OutputName>.*?</OutputName>','<OutputName>h7_mac_CM7_'+mode+'</OutputName>',tail,count=1)
 temp=ROOT/('MDK-ARM/msc41_diag_'+mode+'.uvprojx');assert not temp.exists()
 temp.write_bytes((head+tail).encode('utf-8'))
 try:
  cc=['D:/32/ARM/ARMCC/Bin/armcc.exe','--cpu','Cortex-M7','--c99','-E','-DSTM32H747xx','-DCORE_CM7','-DUSE_HAL_DRIVER',*shlex.split(controls.findtext('MiscControls') or '')]
  for inc in include:cc+=['-I',inc]
  cc+=['-D'+d for d in defs]
  probe=dest/'macro_probe.c';probe.write_text('#include "msc_test_config.h"\nMSC_AUDIT_BOOT USB_MODE_BOOT_MSC\nMSC_AUDIT_SD USB_MSC_SD_TEST\nMSC_AUDIT_WRITE USB_MSC_SD_WRITE_ENABLE\n')
  expanded=run(cc+[str(probe)]);values=dict(re.findall(r'MSC_AUDIT_(BOOT|SD|WRITE)\s+(\d+)',expanded));assert values==dict(BOOT='0' if mode=='uvc' else '1',SD='1' if mode=='sd_readonly' else '0',WRITE='0')
  (dest/'macro_values.i').write_text(expanded,encoding='utf-8')
  sd=run(cc+[str(ROOT/'CM7/Service/usb_msc_sd.c')]);(dest/'usb_msc_sd.i').write_text(sd,encoding='utf-8')
  start=sd.index('static void MscSd_Execute(');brace=sd.index('{',start);depth=1;end=brace+1
  while depth:
   depth+=(sd[end]=='{')-(sd[end]=='}');end+=1
  body=sd[brace:end];assert 'SD_Block_Write(' not in body
  log=dest/'build.log';startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  args=['D:/32/UV4/UV4.exe','-j0','-r',str(temp),'-t','h7_mac_CM7','-o',str(log)]
  result=subprocess.run(args,cwd=ROOT,startupinfo=startup,creationflags=subprocess.CREATE_NO_WINDOW);commands.append(dict(argv=args,exit=result.returncode))
  content=log.read_text(errors='replace');match=re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)',content)
  assert result.returncode==0 and match and match.groups()==('0','0'),content[-4000:]
  image=dest/'.axf';artifact=dest/('h7_mac_CM7_'+mode+'.axf');shutil.copy2(image,artifact)
  hexfile=dest/('h7_mac_CM7_'+mode+'.hex');run(['D:/32/ARM/ARMCC/Bin/fromelf.exe','--i32combined','--output',str(hexfile),str(artifact)])
  shutil.copy2(dest/'.map',dest/('h7_mac_CM7_'+mode+'.map'))
  rows.append(dict(mode=mode,added_sources=missing,defines=defs,preprocessed_values=values,adapter_write_call_present=False,summary=match.group(),artifact=str(artifact),sha256=hashlib.sha256(artifact.read_bytes()).hexdigest()))
  print(rows[-1],flush=True)
 finally:
  temp.unlink();RTE.write_bytes(rte_original);assert PROJECT.read_bytes()==original;assert options.read_bytes()==options_original
(HERE/'build_results.json').write_text(json.dumps(dict(builds=rows,commands=commands,user_project_sha256=hashlib.sha256(original).hexdigest(),user_options_sha256=hashlib.sha256(options_original).hexdigest()),indent=2),encoding='utf-8')
