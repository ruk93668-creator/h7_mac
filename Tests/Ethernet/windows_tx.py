"""Send deterministic raw Ethernet test frames using installed Windows Npcap.
No IP configuration or firmware modification. --dry-run never opens/sends.
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import struct
import subprocess
import time


def frame(source, sequence):
    return bytes.fromhex('024747000001') + source + b'\x88\xb5' + struct.pack('!I', sequence) + bytes(range(42))


class Device(C.Structure):
    pass


Device._fields_ = [('next', C.POINTER(Device)), ('name', C.c_char_p),
                   ('description', C.c_char_p), ('addresses', C.c_void_p), ('flags', C.c_uint)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-mac', required=True, help='Select the actual adapter by its MAC')
    parser.add_argument('--count', type=int, default=1)
    parser.add_argument('--interval-ms', type=float, default=10)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--check', action='store_true', help='Open/verify Npcap adapter without sending')
    args = parser.parse_args()
    source = bytes.fromhex(args.source_mac.replace(':', '').replace('-', ''))
    if len(source) != 6 or not 1 <= args.count <= 10000 or args.interval_ms < 1:
        parser.error('require six MAC bytes, count 1..10000, interval >= 1 ms')
    command = '[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; Get-NetAdapter | Select-Object Name,MacAddress,InterfaceGuid,Status | ConvertTo-Json'
    adapters = json.loads(subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', command]).decode('utf-8-sig'))
    if isinstance(adapters, dict):
        adapters = [adapters]
    matches = [a for a in adapters if a.get('MacAddress', '').replace('-', '').lower() == source.hex()]
    if len(matches) != 1 or matches[0]['Status'] != 'Up':
        raise RuntimeError('Expected exactly one UP adapter matching source MAC')
    adapter = matches[0]
    packet = frame(source, 1)
    assert len(packet) == 60 and packet[14:18] == b'\0\0\0\1' and packet[18:] == bytes(range(42))
    print(json.dumps(dict(adapter=adapter, destination='02:47:47:00:00:01',
                         count=args.count, interval_ms=args.interval_ms, first_frame=packet.hex(' ')), ensure_ascii=True), flush=True)
    if args.dry_run:
        return
    directory = Path(os.environ['SystemRoot']) / 'System32' / 'Npcap'
    with os.add_dll_directory(str(directory)):
        lib = C.CDLL(str(directory / 'wpcap.dll'))
        lib.pcap_findalldevs.argtypes = [C.POINTER(C.POINTER(Device)), C.c_char_p]
        lib.pcap_findalldevs.restype = C.c_int
        lib.pcap_freealldevs.argtypes = [C.POINTER(Device)]
        lib.pcap_freealldevs.restype = None
        lib.pcap_open_live.argtypes = [C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_char_p]
        lib.pcap_open_live.restype = C.c_void_p
        lib.pcap_datalink.argtypes = [C.c_void_p]
        lib.pcap_datalink.restype = C.c_int
        lib.pcap_sendpacket.argtypes = [C.c_void_p, C.c_void_p, C.c_int]
        lib.pcap_sendpacket.restype = C.c_int
        lib.pcap_geterr.argtypes = [C.c_void_p]
        lib.pcap_geterr.restype = C.c_char_p
        lib.pcap_close.argtypes = [C.c_void_p]
        lib.pcap_close.restype = None
        error = C.create_string_buffer(256)
        devices = C.POINTER(Device)()
        if lib.pcap_findalldevs(C.byref(devices), error) != 0:
            raise RuntimeError(error.value.decode(errors='replace'))
        names = []
        try:
            p = devices
            guid = adapter['InterfaceGuid'].strip('{}').lower()
            while p:
                name = bytes(p.contents.name)
                if guid in name.decode(errors='replace').lower():
                    names.append(name)
                p = p.contents.next
        finally:
            lib.pcap_freealldevs(devices)
        if len(names) != 1:
            raise RuntimeError('Npcap device GUID did not match selected adapter uniquely')
        handle = lib.pcap_open_live(names[0], 2048, 0, 100, error)
        if not handle:
            raise RuntimeError(error.value.decode(errors='replace'))
        sent = 0
        try:
            if lib.pcap_datalink(handle) != 1:
                raise RuntimeError('Selected Npcap device is not Ethernet')
            if args.check:
                print(json.dumps(dict(npcap_adapter_verified=True, packets_sent=0)), flush=True)
                return
            for sequence in range(1, args.count + 1):
                packet = frame(source, sequence)
                buffer = C.create_string_buffer(packet)
                if lib.pcap_sendpacket(handle, buffer, len(packet)) != 0:
                    raise RuntimeError(lib.pcap_geterr(handle).decode(errors='replace'))
                sent += 1
                if sequence < args.count:
                    time.sleep(args.interval_ms / 1000)
        finally:
            lib.pcap_close(handle)
            print(json.dumps(dict(submitted_to_npcap=sent, board_receipt_verified=False)), flush=True)


if __name__ == '__main__':
    main()
