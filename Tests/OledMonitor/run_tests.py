"""Compile production CM4 OLED/IPC view code with ARMCC5, then execute in Unicorn."""
from pathlib import Path
import json, subprocess, sys, struct, zlib
ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
OUT=ROOT/'output/OledMonitor'
OUT.mkdir(parents=True,exist_ok=True)
sys.stdout.reconfigure(encoding='utf-8')
sys.path.insert(0,str(ROOT/'Tests/OledAsyncAcceptance/python_deps'))
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_PC
from elftools.elf.elffile import ELFFile
NAMES=[
 'A字形与第六列留白','全部95个ASCII字形','右下角坐标裁剪','非ASCII替换问号','空参数拒绝',
 '刷新BUSY禁止绘图','缺失绘图Ops','清屏失败','像素失败','长画面变短无残留',
 'Y8格式','RGB565格式','YUY2格式','JPEG格式','零FPS','不足1FPS','FPS一位小数','FPS大值',
 '正常四行排版','USB启动但无完成FPS','Camera与USB停止','Camera与USB停止中','IPC初始化',
 'CM7失联','启动超时','协议错误','未知通信状态','读取错误','短暂BUSY缓存','所有来源无效',
 'Camera无效','USB无效','Control无效','Camera陈旧','USB陈旧','Control陈旧','全部陈旧',
 'Camera故障','USB故障','Control故障','真实IPC年龄499/500ms与Tick回绕','计数最大值',
 'INT32_MIN错误码','重复初始化不创建Timer','初始化前轮询','99/100ms启动',
 '异步发送期间不改Framebuffer','提交BUSY恢复原Framebuffer','提交IO错误与重试',
 '完成IO错误与恢复','四页命令数据顺序和字节一致','画面变化等旧帧完成后更新',
 'IPC接口失败不发送','绘图失败恢复原Framebuffer','Timer对象池满','缺失设备','设备未READY',
 '缺失Ops不建Timer','底部裁剪','极大坐标裁剪','A4保留且A5已删除']
assert len(NAMES)==61
commands=[]
def run(args):
 args=list(map(str,args));r=subprocess.run(args,cwd=ROOT,capture_output=True)
 text=(r.stdout+r.stderr).decode(errors='replace')
 commands.append(dict(command=subprocess.list2cmdline(args),exit_code=r.returncode,output=text))
 (HERE/'commands.json').write_text(json.dumps(commands,ensure_ascii=False,indent=2),encoding='utf-8')
 if r.returncode or 'warning:' in text.lower():
  print(text,flush=True)
  raise RuntimeError('Build failed or warning')
 return text
cc=Path('D:/32/ARM/ARMCC/Bin/armcc.exe')
version=run([cc,'--vsn']).strip()
rows=[]
for opt in ['O0','O2']:
 obj=OUT/('monitor_'+opt+'.o');axf=obj.with_suffix('.axf')
 flags=[cc,'--cpu','Cortex-M4','--c99','-'+opt]
 for inc in ['CM4/Device/Inc','CM4/BSP','Common/Inc','Board/Inc','CM4/Application','CM4/Framework','CM4/Platform','CM4/Service']:
  flags+=['-I',ROOT/inc]
 run(flags+['-c',HERE/'monitor_test.c','-o',obj])
 run([cc.with_name('armlink.exe'),obj,'--entry','OledMonitor_TestRun','--scatter',HERE/'test.sct',
      '--keep=*(.framework_init)','--keep=*(.device_export)','--no_scanlib','-o',axf])
 with axf.open('rb') as f:
  elf=ELFFile(f);entry=elf.header['e_entry']
  sections=[(s['sh_addr'],s.data()) for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_size']]
  symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
 for case,name in enumerate(NAMES):
  u=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
  for address,size in [(0x08000000,0x100000),(0x20000000,0x40000),(0x01000000,0x1000)]:u.mem_map(address,size)
  for address,data in sections:u.mem_write(address,data)
  u.reg_write(UC_ARM_REG_SP,0x2003fff0);u.reg_write(UC_ARM_REG_LR,0x01000001);u.reg_write(UC_ARM_REG_R0,case)
  error=None
  try:u.emu_start(entry|1,0x01000000,count=10000000)
  except Exception as exc:error=str(exc)
  checks,line=struct.unpack('<2I',u.mem_read(symbols['g_result'],8))
  passed=not error and not line and u.reg_read(UC_ARM_REG_PC)==0x01000000 and u.reg_read(UC_ARM_REG_R0)==0
  row=dict(case=case,name=name,optimization=opt,result='PASS' if passed else 'FAIL',checks=checks,failure_line=line,error=error)
  rows.append(row)
  if passed and opt=='O0' and case in [18,22,23,33,36,39]:
   address=struct.unpack('<I',u.mem_read(symbols['g_monitor_frame_address'],4))[0]
   frame=u.mem_read(address,512)
   # Render the actual emulated production framebuffer, not a separately drawn UI.
   scale=4;scan=bytearray()
   for y in range(32):
    pixel_line=bytes(255 if frame[(y//8)*128+x]&(1<<(y%8)) else 0 for x in range(128))
    expanded=bytes(v for v in pixel_line for _ in range(scale))
    for _ in range(scale):scan+=b'\0'+expanded
   def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
   png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',512,128,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')
   (HERE/('preview_'+str(case)+'.png')).write_bytes(png)
  print(opt,case,row['result'],name,'line',line,flush=True)
summary=dict(total=len(rows),passed=sum(r['result']=='PASS' for r in rows),failed=sum(r['result']=='FAIL' for r in rows),assertions=sum(r['checks'] for r in rows))
(HERE/'results.json').write_text(json.dumps(dict(compiler=version,summary=summary,cases=rows,hardware_verified=False),ensure_ascii=False,indent=2),encoding='utf-8')
print(summary)
raise SystemExit(bool(summary['failed']))
