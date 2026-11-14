"""Generate one isolated Keil acceptance project; never edit the user's project."""
from pathlib import Path
import xml.etree.ElementTree as E
import re
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/CameraModuleR2/watch_build'

def prepare():
 OUT.mkdir(parents=True,exist_ok=True)
 project=ROOT/'MDK-ARM/h7_mac.uvprojx';tree=E.parse(project);root=tree.getroot();targets=root.find('Targets')
 for target in list(targets):
  if target.findtext('TargetName')!='h7_mac_CM7':targets.remove(target)
 target=targets.find('Target');assert target is not None
 target.find('TargetName').text='h7_mac_CM7_R2_Watch'
 def absolute(value):
  value=value.strip()
  if not value or '$' in value:return value
  return str((project.parent/value).resolve())
 for node in target.iter():
  if not node.text:continue
  if node.tag=='FilePath':node.text=absolute(node.text)
  elif node.tag=='IncludePath':node.text=';'.join(absolute(p) for p in node.text.split(';') if p.strip())
  elif node.tag in ['ScatterFile','InitializationFile','RegisterFile'] and node.text.strip():node.text=absolute(node.text)
  elif node.tag=='MiscControls':
   node.text=re.sub(r'--preinclude\s+([^\s]+)',lambda m:'--preinclude "'+absolute(m[1])+'"',node.text)
 output=target.find('.//TargetCommonOption/OutputDirectory');output.text=str(OUT/'firmware')+'\\'
 target.find('.//TargetCommonOption/OutputName').text='h7_mac_CM7_R2_Watch'
 listing=target.find('.//TargetCommonOption/ListingPath')
 if listing is not None:listing.text=str(OUT/'firmware')+'\\'
 cads=target.find('.//TargetArmAds/Cads/VariousControls');assert cads is not None
 define=cads.find('Define');define.text=(define.text or '').rstrip(', ')+',APP_R2_HW_ACCEPTANCE_ENABLED=1'
 include=cads.find('IncludePath');include.text=(include.text or '')+';'+str(HERE)+';'+str(ROOT/'CM7/Application/Tasks')
 group=E.SubElement(target.find('Groups'),'Group');E.SubElement(group,'GroupName').text='R2 Hardware Acceptance Only'
 files=E.SubElement(group,'Files');f=E.SubElement(files,'File')
 E.SubElement(f,'FileName').text='HardwareControl.c';E.SubElement(f,'FileType').text='1';E.SubElement(f,'FilePath').text=str(HERE/'HardwareControl.c')
 path=OUT/'h7_mac_r2_watch.uvprojx';E.indent(tree,space='  ');tree.write(path,encoding='utf-8',xml_declaration=True)
 # 首次生成时复用本机CM7调试器选择，后续保留验收工程自己的用户设置。
 original_options=project.with_suffix('.uvoptx');options=path.with_suffix('.uvoptx')
 if original_options.exists() and not options.exists():
  settings=E.parse(original_options);settings_root=settings.getroot()
  for item in list(settings_root.findall('Target')):
   if item.findtext('TargetName')!='h7_mac_CM7':settings_root.remove(item)
  selected=settings_root.find('Target')
  if selected is not None:
   selected.find('TargetName').text='h7_mac_CM7_R2_Watch'
   # 只清理新副本的旧断点、绝对地址Watch和初始化脚本，不触碰原设置。
   option=selected.find('TargetOption')
   for tag in ['Breakpoint','WatchWindow1','MemoryWindow1','Tracepoint']:
    node=option.find(tag)
    if node is not None:node.clear()
   for tag in ['sIfile','tIfile']:
    node=option.find('DebugOpt/'+tag)
    if node is not None:node.text=''
   E.indent(settings,space='  ');settings.write(options,encoding='utf-8',xml_declaration=True)
 return path

if __name__=='__main__':print(prepare())
