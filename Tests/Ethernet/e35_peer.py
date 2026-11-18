"""Windows Npcap raw-frame peer. Saves raw PCAP and PC observations, never board PASS.

Frame lengths exclude FCS. VLAN 1518 here is 1522 on the wire including FCS.
Peer MAC is fixed by the independent bench protocol; this script verifies it.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

BOARD = bytes.fromhex('024747000001')
PEER = bytes.fromhex('ec1ac33edbb2')


def frame(session, sequence, length, vlan=False, from_board=False):
    if not 0 < session <= 0xffffffff or not 0 < sequence <= 0xffffffff or not 60 <= length <= 1522:
        raise ValueError('invalid session, sequence or frame length')
    header = (PEER + BOARD if from_board else BOARD + PEER)
    header += b'\x81\x00\x00\x07' if vlan else b''
    header += b'\x88\xb5E35!' + struct.pack('!IIH', session, sequence, length)
    return header + bytes((i ^ sequence ^ (sequence >> 8) ^ session) & 255
                          for i in range(len(header), length))


def inspect(packet, session, length, vlan, from_board=True):
    """Return sequence, None for unrelated packets, or raise for corrupt board frames."""
    offset = 18 if packet[12:14] == b'\x81\x00' else 14
    if len(packet) < offset + 14 or packet[6:12] != (BOARD if from_board else PEER):
        return None
    if packet[offset - 2:offset + 4] != b'\x88\xb5E35!':
        return None
    actual_session, sequence, declared = struct.unpack('!IIH', packet[offset + 4:offset + 14])
    if actual_session != session:
        return None
    if declared != length or bool(offset == 18) != vlan or packet != frame(session, sequence, length, vlan, from_board):
        raise ValueError('board frame length/header/payload mismatch')
    return sequence


class Header(C.Structure):
    # Windows timeval uses two 32-bit long members even on x64.
    _fields_ = [('seconds', C.c_int32), ('microseconds', C.c_int32),
                ('caplen', C.c_uint32), ('length', C.c_uint32)]


class Stats(C.Structure):
    # Include the Npcap extension fields; only the first three are used by pcap_stats.
    _fields_ = [('received', C.c_uint), ('dropped', C.c_uint), ('interface_dropped', C.c_uint),
                ('captured', C.c_uint), ('sent', C.c_uint), ('network_dropped', C.c_uint)]


def open_adapter():
    command = '[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; Get-NetAdapter | Select Name,MacAddress,InterfaceGuid,Status | ConvertTo-Json'
    adapters = json.loads(subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', command]).decode('utf-8-sig'))
    if isinstance(adapters, dict):
        adapters = [adapters]
    selected = [item for item in adapters if item.get('MacAddress', '').replace('-', '').lower() == PEER.hex() and item['Status'] == 'Up']
    if len(selected) != 1:
        raise RuntimeError('require exactly one UP adapter EC-1A-C3-3E-DB-B2')
    adapter = selected[0]
    directory = Path(os.environ['SystemRoot']) / 'System32/Npcap'
    dll_directory = os.add_dll_directory(str(directory))
    lib = C.CDLL(str(directory / 'wpcap.dll'))
    signatures = {
        'pcap_open_live': ([C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_char_p], C.c_void_p),
        'pcap_datalink': ([C.c_void_p], C.c_int),
        'pcap_setnonblock': ([C.c_void_p, C.c_int, C.c_char_p], C.c_int),
        'pcap_sendpacket': ([C.c_void_p, C.c_void_p, C.c_int], C.c_int),
        'pcap_next_ex': ([C.c_void_p, C.POINTER(C.POINTER(Header)), C.POINTER(C.POINTER(C.c_ubyte))], C.c_int),
        'pcap_stats': ([C.c_void_p, C.POINTER(Stats)], C.c_int),
        'pcap_geterr': ([C.c_void_p], C.c_char_p),
        'pcap_close': ([C.c_void_p], None),
    }
    for name, (arguments, result) in signatures.items():
        getattr(lib, name).argtypes = arguments
        getattr(lib, name).restype = result
    error = C.create_string_buffer(256)
    guid = adapter['InterfaceGuid'].strip('{}')
    handle = lib.pcap_open_live(('\\Device\\NPF_{' + guid + '}').encode(), 2048, 0, 10, error)
    if not handle:
        raise RuntimeError(error.value.decode(errors='replace'))
    if lib.pcap_datalink(handle) != 1 or lib.pcap_setnonblock(handle, 1, error) != 0:
        lib.pcap_close(handle)
        raise RuntimeError('Npcap Ethernet/nonblocking configuration failed')
    return lib, handle, adapter, dll_directory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--session', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--length', type=int, choices=[60, 512, 1514, 1518, 1522], default=60)
    parser.add_argument('--vlan', action='store_true')
    parser.add_argument('--count', type=int, default=1000, help='0 captures only')
    parser.add_argument('--interval-ms', type=float, default=10)
    parser.add_argument('--delay-seconds', type=float, default=5)
    parser.add_argument('--capture-seconds', type=float, default=20, help='minimum; capture also continues until sending ends + 3 s')
    parser.add_argument('--output', type=Path, required=True, help='new directory, never overwrite evidence')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.count <= 1000000 or args.interval_ms < 1 or args.capture_seconds <= 0 or args.delay_seconds < 0:
        parser.error('invalid count/timing')
    first = frame(args.session, 1, args.length, args.vlan)
    if args.dry_run:
        print(json.dumps(dict(length=len(first), first_frame=first.hex(), sends_packets=False)))
        return
    args.output.mkdir(parents=True, exist_ok=False)
    summary = dict(config=vars(args).copy(), sent=0, board_received=0, corrupt=0,
                   duplicates=0, reordered=0, highest=0, complete=False,
                   board_receipt_verified=False, hardware_pass=False)
    summary['config']['output'] = str(args.output)
    seen = set()
    pc_seen = set()
    time_ranges = {'board': [None, None], 'pc': [None, None]}
    lib, handle, adapter, dll_directory = open_adapter()
    summary['adapter'] = adapter
    pcap_path = args.output / 'capture.pcap'
    started = time.monotonic()
    next_send = started + args.delay_seconds
    last_send = None
    try:
        with pcap_path.open('xb') as capture:
            capture.write(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 2048, 1))
            print('CAPTURE_READY: CPU must keep running; enable board TX now if required.', flush=True)
            while True:
                now = time.monotonic()
                if summary['sent'] < args.count and now >= next_send:
                    packet = frame(args.session, summary['sent'] + 1, args.length, args.vlan)
                    buffer = C.create_string_buffer(packet)
                    if lib.pcap_sendpacket(handle, buffer, len(packet)) != 0:
                        raise RuntimeError(lib.pcap_geterr(handle).decode(errors='replace'))
                    summary['sent'] += 1
                    last_send = time.monotonic()
                    next_send = last_send + args.interval_ms / 1000
                for _ in range(256):
                    header = C.POINTER(Header)()
                    data = C.POINTER(C.c_ubyte)()
                    status = lib.pcap_next_ex(handle, C.byref(header), C.byref(data))
                    if status == 0:
                        break
                    if status != 1:
                        raise RuntimeError('Npcap receive failed')
                    item = header.contents
                    packet = C.string_at(data, item.caplen)
                    capture.write(struct.pack('<IIII', item.seconds, item.microseconds, item.caplen, item.length))
                    capture.write(packet)
                    from_board = packet[6:12] == BOARD
                    try:
                        sequence = inspect(packet, args.session, args.length, args.vlan, from_board)
                    except ValueError:
                        summary['corrupt'] += 1
                        continue
                    if sequence is None:
                        continue
                    times = time_ranges['board' if from_board else 'pc']
                    timestamp = item.seconds + item.microseconds / 1000000
                    if times[0] is None:
                        times[0] = timestamp
                    times[1] = timestamp
                    if not from_board:
                        pc_seen.add(sequence)
                        continue
                    summary['board_received'] += 1
                    if sequence in seen:
                        summary['duplicates'] += 1
                    elif sequence < summary['highest']:
                        summary['reordered'] += 1
                    seen.add(sequence)
                    summary['highest'] = max(sequence, summary['highest'])
                now = time.monotonic()
                if summary['sent'] == args.count and now - started >= args.capture_seconds and (last_send is None or now - last_send >= 3):
                    summary['complete'] = True
                    break
                time.sleep(0.0005)
    finally:
        stats = Stats()
        summary['npcap_stats'] = dict(received=stats.received, dropped=stats.dropped, interface_dropped=stats.interface_dropped) if lib.pcap_stats(handle, C.byref(stats)) == 0 else None
        lib.pcap_close(handle)
        dll_directory.close()
        summary['elapsed_seconds'] = time.monotonic() - started
        summary['board_unique'] = len(seen)
        summary['pc_captured_unique'] = len(pc_seen)
        summary['capture_time_ranges'] = time_ranges
        if all(value is not None for times in time_ranges.values() for value in times):
            summary['bidirectional_overlap_seconds'] = max(0, min(times[1] for times in time_ranges.values()) - max(times[0] for times in time_ranges.values()))
        else:
            summary['bidirectional_overlap_seconds'] = None
        summary['internal_missing'] = summary['highest'] - len(seen)
        summary['trailing_missing'] = 'requires board tx_accepted snapshot'
        summary['pcap_sha256'] = hashlib.sha256(pcap_path.read_bytes()).hexdigest() if pcap_path.exists() else None
        (args.output / 'peer.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        print(json.dumps(summary, ensure_ascii=True), flush=True)


if __name__ == '__main__':
    main()
