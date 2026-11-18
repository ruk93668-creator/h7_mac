from pathlib import Path
import subprocess,json,hashlib,collections
ROOT=Path.cwd();source=ROOT/'output/MSC24/msc21/msc21-enum-01.pcapng';tool='C:/Program Files/Wireshark/tshark.exe'
args=[tool,'-r',str(source),'-Y','usb.bus_id == 1 && usb.device_address == 53','-T','json','-x']
packets=json.loads(subprocess.check_output(args));records=[];active=None
for packet in packets:
 layer=packet['_source']['layers'];raw=bytes.fromhex(layer['frame_raw'][0]);header=int.from_bytes(raw[:2],'little');payload=raw[header:];number=int(layer['frame']['frame.number'])
 if raw[22]!=3:continue
 if raw[21]==2 and raw[16]==0 and len(payload)==31 and payload[:4]==b'USBC':
  active=dict(cbw_frame=number,tag=int.from_bytes(payload[4:8],'little'),host_length=int.from_bytes(payload[8:12],'little'),cdb_length=payload[14],cdb=payload[15:15+payload[14]].hex(),opcode=payload[15],data_length=0,data_prefix='',data_frames=[])
 elif active is not None and raw[21]==0x82 and raw[16]==1 and payload:
  if len(payload)==13 and payload[:4]==b'USBS':
   active.update(csw_frame=number,csw_tag=int.from_bytes(payload[4:8],'little'),residue=int.from_bytes(payload[8:12],'little'),status=payload[12]);records.append(active);active=None
  else:
   if not active['data_prefix']:active['data_prefix']=payload[:64].hex()
   active['data_length']+=len(payload);active['data_frames'].append(number)
chains=[]
for previous,current in zip(records,records[1:]):
 if previous['opcode']==0x23 and current['opcode']==3:
  data=bytes.fromhex(current['data_prefix']);good=current['cdb_length']==12 and current['data_length']==18 and current['status']==0 and current['residue']==0 and len(data)==18 and (data[0],data[2],data[7],data[12],data[13])==(0x70,5,10,0x20,0)
  chains.append(dict(unsupported=previous,sense=current,passed=good))
capacity=[r for r in records if r['opcode']==0x25 and r['status']==0];reads=[r for r in records if r['opcode']==0x28 and r['status']==0]
report=dict(source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),bus=1,address=53,target_packets=len(packets),completed_commands=len(records),failed_commands=[r for r in records if r["status"]!=0],sense_records=[r for r in records if r["opcode"]==3],opcode_counts=dict(collections.Counter(hex(r['opcode']) for r in records)),status_counts=dict(collections.Counter(r['status'] for r in records)),all_tags_match=all(r['tag']==r['csw_tag'] for r in records),sense_after_unsupported=chains,first_successful_capacity=capacity[0] if capacity else None,successful_capacity_count=len(capacity),first_successful_read=reads[0] if reads else None,successful_read_count=len(reads),incomplete_last_command=active)
Path('Tests/MscSenseCompat/hardware_results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('Commands',len(records),'Counts',report['opcode_counts'],'Statuses',report['status_counts'],'Tagmatch',report['all_tags_match'])
print('0x23 -> REQUEST SENSE chains:',[(x['unsupported']['cbw_frame'],x['sense']['cbw_frame'],x['sense']['data_frames'],x['sense']['csw_frame'],x['passed']) for x in chains])
print('First capacity:',report['first_successful_capacity']);print('Successful reads',len(reads),'first',report['first_successful_read'])
assert chains and all(x['passed'] for x in chains)
subprocess.run([tool,'-r',str(source),'-Y','usb.bus_id == 1 && usb.device_address == 53','-w',str(ROOT/'Tests/MscSenseCompat/after_fix.pcapng')],check=True)
