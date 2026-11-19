"""Stream metadata only; SD file payload is not copied into reports."""
from pathlib import Path
import subprocess,csv,sys,json,collections,hashlib
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'output/MSC4-readonly';source=Path(sys.argv[1])
fields=['frame.number','frame.time_relative','usb.bus_id','usb.device_address','usb.endpoint_address','usb.irp_info.direction','usb.data_len','usb.usbd_status','usb.idVendor','usb.idProduct','usbms.dCBWSignature','usbms.dCSWSignature','usbms.dCBWTag','usbms.dCBWDataTransferLength','usbms.dCBWCBLength','usbms.dCSWStatus','usbms.dCSWDataResidue','scsi.spc.opcode','scsi_sbc.opcode','scsi_sbc.rdwr10.lba','scsi_sbc.rdwr10.xferlen','scsi.sns.key','scsi.sns.asc','scsi.sns.ascq','usbms.setup.bRequest','usb.setup.bRequest','usb.setup.wIndex','usb.bmRequestType','scsi_sbc.returned_lba','scsi_sbc.blocksize']
cmd=['C:/Program Files/Wireshark/tshark.exe','-n','-r',str(source),'-T','fields','-E','separator=\t','-E','quote=d','-E','occurrence=f']
for field in fields:cmd+=['-e',field]
process=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',errors='replace')
def integer(value):
 try:return int(value,0) if value.startswith('0x') else int(value)
 except (ValueError,AttributeError):return None
active={};records=[];addresses=set();controls=[];errors=[];sense=[];capacities=[];overlap=[]
for values in csv.reader(process.stdout,delimiter='\t',quotechar='"'):
 r=dict(zip(fields,values));addr=(integer(r.get('usb.bus_id','')),integer(r.get('usb.device_address','')));frame=integer(r.get('frame.number',''))
 if integer(r.get('usb.idVendor',''))==0x1209 and integer(r.get('usb.idProduct',''))==1:addresses.add(addr)
 if r.get('usbms.dCBWSignature'):
  if addr in active:overlap.append(dict(address=addr,previous=active[addr]['cbw_frame'],new=frame))
  active[addr]=dict(address=addr,cbw_frame=frame,time=r['frame.time_relative'],tag=integer(r['usbms.dCBWTag']),host_bytes=integer(r['usbms.dCBWDataTransferLength']),cdb_length=integer(r['usbms.dCBWCBLength']),opcode=integer(r.get('scsi.spc.opcode') or r.get('scsi_sbc.opcode') or ''),lba=integer(r.get('scsi_sbc.rdwr10.lba','')),blocks=integer(r.get('scsi_sbc.rdwr10.xferlen','')),data_bytes=0,data_frames=[])
 elif r.get('usbms.dCSWSignature') and addr in active:
  rec=active.pop(addr);rec.update(csw_frame=frame,csw_tag=integer(r['usbms.dCBWTag']),status=integer(r['usbms.dCSWStatus']),residue=integer(r['usbms.dCSWDataResidue']));records.append(rec)
 elif addr in active and integer(r.get('usb.endpoint_address',''))==0x82 and integer(r.get('usb.irp_info.direction',''))==1:
  size=integer(r.get('usb.data_len','')) or 0
  if size:
   active[addr]['data_bytes']+=size
   if len(active[addr]['data_frames'])<8:active[addr]['data_frames'].append(frame)
 if r.get('scsi.sns.key'):sense.append(dict(frame=frame,address=addr,key=integer(r['scsi.sns.key']),asc=integer(r.get('scsi.sns.asc','')),ascq=integer(r.get('scsi.sns.ascq',''))))
 if r.get('scsi_sbc.returned_lba'):capacities.append(dict(frame=frame,address=addr,last_lba=integer(r['scsi_sbc.returned_lba']),block_size=integer(r.get('scsi_sbc.blocksize',''))))
 request=r.get('usbms.setup.bRequest') or r.get('usb.setup.bRequest')
 if request:controls.append(dict(frame=frame,address=addr,request=integer(request),bm=integer(r.get('usb.bmRequestType','')),index=r.get('usb.setup.wIndex')))
 if integer(r.get('usb.usbd_status','')) not in (None,0):errors.append(dict(frame=frame,address=addr,status=r['usb.usbd_status']))
err=process.stderr.read();code=process.wait();target=[r for r in records if tuple(r['address']) in addresses];reads=[r for r in target if r['opcode']==0x28]
interesting=[r for r in target if r['status']!=0 or r['opcode']!=0x28 or (r['lba'],r['blocks']) in [(0,3),(0,64),(63,4),(1024,64),(15224831,1),(15224832,1)]]
report=dict(source=str(source),tshark_exit=code,tshark_error=err,addresses=sorted(addresses),completed_commands=len(target),opcode_counts=dict(collections.Counter(hex(r['opcode']) if r['opcode'] is not None else 'unknown' for r in target)),status_counts=dict(collections.Counter(r['status'] for r in target)),all_completed_tags_match=all(r['tag']==r['csw_tag'] for r in target),read_count=len(reads),read_blocks=sum(r['blocks'] or 0 for r in reads),read_block_histogram=dict(collections.Counter(r['blocks'] for r in reads)),read_data_length_mismatches=[r for r in reads if r['status']==0 and r['data_bytes']!=r['host_bytes']],expected_two_sector_requests=sum(((r['blocks'] or 0)+1)//2 for r in reads if r['status']==0),interesting=interesting,sense=[r for r in sense if tuple(r['address']) in addresses],capacities=[r for r in capacities if tuple(r['address']) in addresses],controls=[r for r in controls if tuple(r['address']) in addresses],transfer_errors=[r for r in errors if tuple(r['address']) in addresses],overlapping_cbws=overlap,incomplete=list(active.values()))
# These expected batch counts are inferred until correlated with an actual debugger trace.
(OUT/'capture_metadata.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:report[k] for k in ['tshark_exit','addresses','completed_commands','opcode_counts','status_counts','all_completed_tags_match','read_count','read_block_histogram','expected_two_sector_requests']},indent=2));print('Read length mismatches',len(report['read_data_length_mismatches']))
