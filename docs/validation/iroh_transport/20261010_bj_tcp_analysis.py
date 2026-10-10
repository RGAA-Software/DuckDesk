from pathlib import Path
import collections,json,socket,struct
capture_path=Path('C:/Users/chess/AppData/Local/PixelsRelayBuild/bj-stutter-20261010/relay.pcap')
flows={}
def flow_state(key):
    return flows.setdefault(key,{'payload_bytes':0,'packets':0,'next_sequence':None,'pending':[], 'hole':None,'holes':[], 'overlap_packets':0,'sack_hole':None,'sack_stalls':[],'zero_window':0})
with capture_path.open('rb') as capture:
    header=capture.read(24)
    endian='<' if header[:4]==b'\xd4\xc3\xb2\xa1' else '>'
    link_type=struct.unpack(endian+'I',header[20:24])[0]
    assert link_type==1,link_type
    while packet_header:=capture.read(16):
        seconds,micros,captured_size,wire_size=struct.unpack(endian+'IIII',packet_header)
        packet=capture.read(captured_size);timestamp=seconds+micros/1e6
        if len(packet)<54 or packet[12:14]!=b'\x08\x00':continue
        ip_offset=14;ip_header_size=(packet[ip_offset]&15)*4
        if packet[ip_offset+9]!=6:continue
        total_size=struct.unpack('!H',packet[ip_offset+2:ip_offset+4])[0]
        source=socket.inet_ntoa(packet[ip_offset+12:ip_offset+16]);destination=socket.inet_ntoa(packet[ip_offset+16:ip_offset+20])
        tcp_offset=ip_offset+ip_header_size
        source_port,destination_port,sequence,acknowledgement=struct.unpack('!HHII',packet[tcp_offset:tcp_offset+12])
        tcp_header_size=(packet[tcp_offset+12]>>4)*4
        flags=packet[tcp_offset+13];payload_size=total_size-ip_header_size-tcp_header_size
        key=f'{source}:{source_port}>{destination}:{destination_port}';reverse=f'{destination}:{destination_port}>{source}:{source_port}'
        state=flow_state(key);reverse_state=flow_state(reverse)
        state['packets']+=1
        if flags & 16 and not flags & 6 and struct.unpack('!H',packet[tcp_offset+14:tcp_offset+16])[0]==0:state['zero_window']+=1
        if flags&2:state['next_sequence']=sequence+1
        options=packet[tcp_offset+20:tcp_offset+tcp_header_size];option_offset=0;sacks=[]
        while option_offset<len(options):
            kind=options[option_offset]
            if kind==0:break
            if kind==1:option_offset+=1;continue
            if option_offset+1>=len(options):break
            length=options[option_offset+1]
            if length<2:break
            if kind==5:
                blocks=options[option_offset+2:option_offset+length]
                for block_offset in range(0,len(blocks)-7,8):sacks.append(struct.unpack('!II',blocks[block_offset:block_offset+8]))
            option_offset+=length
        if reverse_state['sack_hole'] and acknowledgement>reverse_state['sack_hole']['ack']:
            stalled=reverse_state['sack_hole'];stalled['duration_ms']=(timestamp-stalled['time'])*1000
            reverse_state['sack_stalls'].append(stalled);reverse_state['sack_hole']=None
        if any(lower>acknowledgement for lower,upper in sacks) and reverse_state['sack_hole'] is None:
            reverse_state['sack_hole']={'time':timestamp,'ack':acknowledgement}
        if payload_size<=0:continue
        state['payload_bytes']+=payload_size
        if state['next_sequence'] is None:state['next_sequence']=sequence
        next_sequence=state['next_sequence'];end_sequence=sequence+payload_size
        if sequence<next_sequence:state['overlap_packets']+=1
        state['pending'].append((sequence,end_sequence));state['pending'].sort()
        remaining=[]
        for lower,upper in state['pending']:
            if lower<=next_sequence:next_sequence=max(next_sequence,upper)
            else:remaining.append((lower,upper))
        state['pending']=remaining;state['next_sequence']=next_sequence
        if state['hole'] and next_sequence>state['hole']['sequence']:
            missing=state['hole'];missing['duration_ms']=(timestamp-missing['time'])*1000
            state['holes'].append(missing);state['hole']=None
        if remaining and state['hole'] is None:state['hole']={'time':timestamp,'sequence':next_sequence}
summary={key:{field:value for field,value in state.items() if field not in ('pending','next_sequence','hole','sack_hole')} for key,state in flows.items()}
for state in summary.values():
    state['holes']=[event for event in state['holes'] if event['duration_ms']>20]
    state['sack_stalls']=[event for event in state['sack_stalls'] if event['duration_ms']>20]
Path('docs/validation/iroh_transport/20261010_bj_tcp_capture_analysis.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
for key,state in summary.items():
    if state['payload_bytes']>100000:
        print(json.dumps({'flow':key,'bytes':state['payload_bytes'],'overlap_packets':state['overlap_packets'],'zero_window':state['zero_window'],'max_hole_ms':max((event['duration_ms'] for event in state['holes']),default=0),'max_sack_stall_ms':max((event['duration_ms'] for event in state['sack_stalls']),default=0)}))

