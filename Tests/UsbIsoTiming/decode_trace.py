"""Decode a frozen timing capture exported by Keil; never generates USB/video data."""
from pathlib import Path
import argparse,struct,json,csv,statistics
BASE=0x24020000
EVENTS={1:'DATAIN',2:'CONFIRM',3:'SUBMIT',4:'HAL_ENTER',5:'HAL_EXIT',6:'RESULT',7:'FRAME'}
def load(path):
    data=Path(path).read_bytes()
    if not data.lstrip().startswith(b':'):
        return None,data
    memory={};upper=0
    for line in data.decode('ascii').splitlines():
        if not line.strip():continue
        row=bytes.fromhex(line.strip()[1:])
        if len(row)<5 or len(row)!=row[0]+5 or sum(row)&255:raise ValueError('Invalid Intel HEX checksum/length')
        size=row[0];addr=int.from_bytes(row[1:3],'big');typ=row[3];payload=row[4:4+size]
        if typ==0:
            for i,value in enumerate(payload):
                at=upper+addr+i
                if at in memory and memory[at]!=value:raise ValueError('Conflicting HEX address')
                memory[at]=value
        elif typ==4:upper=int.from_bytes(payload,'big')<<16
        elif typ==2:upper=int.from_bytes(payload,'big')<<4
        elif typ==1:break
        elif typ not in (3,5):raise ValueError('Unsupported HEX record')
    if not memory:raise ValueError('Empty HEX')
    first,last=min(memory),max(memory)
    if len(memory)!=last-first+1:raise ValueError('HEX has missing bytes')
    return first,bytes(memory[x] for x in range(first,last+1))

def decode(raw,meta):
    if len(meta)<64:raise ValueError('Metadata needs 64 bytes')
    fields=['enabled','request','state','count','capacity','record_bytes','buffer_address','buffer_bytes','cpu_hz','writers','rejected_requests','start_cycles','end_cycles','stop_reason','magic','version']
    header=dict(zip(fields,struct.unpack_from('<16I',meta)))
    if header['magic']!=0x5554494D or header['version']!=2:raise ValueError('Unknown trace format')
    if header['state']!=3 or header['writers']!=0:raise ValueError('Capture is not READY')
    if not 0<header['count']<=512 or header['record_bytes']!=48 or header['cpu_hz']==0:raise ValueError('Invalid metadata dimensions/clock')
    if header['buffer_address']!=BASE or header['buffer_bytes']!=24576:raise ValueError('Unexpected trace buffer')
    if len(raw)<header['count']*48:raise ValueError('Trace is truncated')
    keys=['cycles','frame','event','sequence','detail','diepctl','dieptsiz','fifo_words','systick_val','ipsr','wall_us','time_flags']
    rows=[]
    for i in range(header['count']):
        row=dict(zip(keys,struct.unpack_from('<12I',raw,i*48)))
        if row['event'] not in EVENTS:raise ValueError('Uncommitted/invalid record '+str(i))
        row['index']=i;row['offset_cycles']=(row['cycles']-header['start_cycles'])&0xffffffff
        row['dwt_relative_us']=row['offset_cycles']*1e6/header['cpu_hz'];row['event_name']=EVENTS[row['event']]
        rows.append(row)
    rows.sort(key=lambda x:(x['offset_cycles'],x['index']))
    wall_start=next((x['wall_us'] for x in rows if x['time_flags']&1),0)
    for row in rows:row['wall_relative_us']=((row['wall_us']-wall_start)&0xffffffff) if row['time_flags']&1 else None
    def elapsed(a,b):
        if not (a['time_flags']&1 and b['time_flags']&1):return None
        return (b['wall_us']-a['wall_us'])&0xffffffff
    def stats(values):
        values=[x for x in values if x is not None]
        if not values:return dict(samples=0)
        ordered=sorted(values)
        return dict(samples=len(values),min_us=min(values),mean_us=statistics.mean(values),median_us=statistics.median(values),p95_us=ordered[min(len(ordered)-1,int((len(ordered)-1)*0.95))],max_us=max(values))
    completions={};confirms={};submits={};hal_enter=None;last_submit=None;hal_exits={};frames=set();metrics={k:[] for k in ['datain_to_confirm','confirm_to_next_submit_within_frame','frame_confirm_to_next_submit','submit_to_datain','submit_to_hal_enter','hal_enter_to_exit','hal_exit_to_datain']};crossings=[];errors=[]
    for row in rows:
        seq=row['sequence'];kind=row['event']
        if kind==1:
            completions[seq]=row
            if seq in submits:metrics['submit_to_datain'].append(elapsed(submits[seq],row))
            if seq in hal_exits:metrics['hal_exit_to_datain'].append(elapsed(hal_exits[seq],row))
        elif kind==2:
            confirms[seq]=row
            if seq in completions:metrics['datain_to_confirm'].append(elapsed(completions[seq],row))
        elif kind==3:
            previous=(seq-1)&0xffffffff
            if previous in confirms:
                name='frame_confirm_to_next_submit' if previous in frames else 'confirm_to_next_submit_within_frame'
                metrics[name].append(elapsed(confirms[previous],row))
            submits[seq]=row;last_submit=row
        elif kind==4:
            hal_enter=row
            if last_submit:metrics['submit_to_hal_enter'].append(elapsed(last_submit,row))
        elif kind==5:
            if hal_enter is not None:
                metrics['hal_enter_to_exit'].append(elapsed(hal_enter,row))
                if hal_enter['frame']!=row['frame']:crossings.append(dict(sequence=last_submit['sequence'] if last_submit else None,before=hal_enter['frame'],after=row['frame']))
            if last_submit:hal_exits[last_submit['sequence']]=row
            if row['detail']!=0:errors.append(row)
            hal_enter=None
        elif kind==6:errors.append(row)
        elif kind==7:frames.add(seq)
    report=dict(metadata=header,invalid_wall_records=sum(not (x['time_flags']&1) for x in rows),event_counts={EVENTS[k]:sum(x['event']==k for x in rows) for k in EVENTS},metrics={k:stats(v) for k,v in metrics.items()},frame_crossings_during_hal=crossings,submission_errors=errors,notes=['Wall-clock intervals use existing TIM6/HAL Tick; DWT may stop in WFI. HAL interval includes recorder overhead; no claim about exact internal FIFO-write instant.','Header and register reads are sequential, not one atomic hardware snapshot.','Only matched complete pairs are measured; capture edge pairs may be absent.','This is a short timing sample, not long-run FPS or hardware acceptance.'])
    return report,rows

def main():
    p=argparse.ArgumentParser();p.add_argument('trace');p.add_argument('--meta',required=True);p.add_argument('--output',default='output/UsbIsoTiming/capture');a=p.parse_args()
    base,raw=load(a.trace);_,meta=load(a.meta)
    if base is not None and base!=BASE:raise ValueError('Wrong trace HEX base')
    report,rows=decode(raw,meta);out=Path(a.output);out.parent.mkdir(parents=True,exist_ok=True)
    out.with_suffix('.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    with out.with_suffix('.csv').open('w',newline='',encoding='utf-8-sig') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
