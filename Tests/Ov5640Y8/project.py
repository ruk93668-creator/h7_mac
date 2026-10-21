from pathlib import Path
import re
p=Path('CM7/Device/Src/ov5640_device.c');s=p.read_bytes().decode('gbk').replace('{0x3035U, 0x11U}','{0x3035U, 0x41U}').replace('{0x3018U, 0xFFU}','{0x3018U, 0xF3U}');p.write_bytes(s.encode('gbk'))
p=Path('MDK-ARM/h7_mac.uvprojx');s=p.read_bytes().decode('utf-8')
for anchor,files in [('ov5640_device.h',[('ov5640.c','Device/Components/OV5640'),('ov5640_reg.c','Device/Components/OV5640')]),('app_main.c',[('app_camera.c','Application'),('app_camera.h','Application')])]:
 pattern=r'(<File>\s*<FileName>'+re.escape(anchor)+r'</FileName>.*?<FilePath>../CM7/.*?</FilePath>\s*</File>)'
 block=''
 for f,d in files:block+='\n            <File>\n              <FileName>'+f+'</FileName>\n              <FileType>'+('1' if f.endswith('.c') else '5')+'</FileType>\n              <FilePath>../CM7/'+d+'/'+f+'</FilePath>\n            </File>'
 s,n=re.subn(pattern,lambda m:m.group(1)+block,s,flags=re.S);assert n==2,(anchor,n)
s=s.replace('..\\CM7\\Framework</IncludePath>','..\\CM7\\Framework;..\\CM7\\Device\\Components\\OV5640</IncludePath>');p.write_bytes(s.encode('utf-8'))
