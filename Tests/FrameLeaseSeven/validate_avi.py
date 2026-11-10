from pathlib import Path
import struct,json,subprocess,shutil
r=Path.cwd();h=r/'Tests/FrameLeaseSeven';p=r/'output/FrameLeaseSeven/sample_O2.avi';b=p.read_bytes();checks={}
u32=lambda o:struct.unpack_from('<I',b,o)[0]
checks['riff']=b[:4]==b'RIFF' and b[8:12]==b'AVI ' and u32(4)+8==len(b)
checks['hdrl']=b[12:16]==b'LIST' and b[20:24]==b'hdrl' and 12+8+u32(16)==212
checks['stream']=b[108:116]==b'vidsMJPG' and u32(176)==320 and u32(180)==240
checks['rate']=u32(132)==2000 and u32(128)==200 and u32(32)==100000
pos=224;frames=[];end=220+u32(216)
while pos<end:
 assert b[pos:pos+4]==b'00dc';n=u32(pos+4);frames.append((pos,n));assert b[pos+8:pos+8+n]==(r/'Tests/JpegStorageStage08D/fixtures/sample.jpg').read_bytes();assert n%2==0 or b[pos+8+n]==0;pos+=8+n+(n&1)
checks['two_frames']=len(frames)==2 and u32(48)==2 and u32(140)==2
checks['index']=b[end:end+4]==b'idx1' and u32(end+4)==len(frames)*16 and end+8+len(frames)*16==len(b)
for i,(off,n) in enumerate(frames):
 at=end+8+i*16;checks['index_'+str(i)]=b[at:at+4]==b'00dc' and u32(at+4)==0x10 and u32(at+8)==off-220 and u32(at+12)==n
ffprobe=shutil.which('ffprobe');ffmpeg=shutil.which('ffmpeg');assert ffprobe and ffmpeg
args=[ffprobe,'-v','error','-count_frames','-show_entries','stream=codec_name,width,height,r_frame_rate,avg_frame_rate,nb_read_frames','-of','json',str(p)]
q=subprocess.run(args,capture_output=True,text=True);info=json.loads(q.stdout);checks['ffprobe']=q.returncode==0 and info['streams'][0]['codec_name']=='mjpeg' and info['streams'][0]['nb_read_frames']=='2'
args2=[ffmpeg,'-v','error','-xerror','-i',str(p),'-f','null','-'];q2=subprocess.run(args2,capture_output=True,text=True);checks['decode']=q2.returncode==0
result=dict(total=len(checks),passed=sum(checks.values()),checks=checks,probe=info,commands=[args,args2],decoder_stderr=q2.stderr,synthetic_fixture=True,hardware_verified=False)
(h/'avi_validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(result);assert all(checks.values())
