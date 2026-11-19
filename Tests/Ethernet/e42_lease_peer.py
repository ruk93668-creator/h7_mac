"""Bench-only short lease responder: fixed PC/board MAC and isolated Ethernet."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time

from e35_peer import BOARD, PEER, Header, Stats, open_adapter

SERVER = bytes((192, 168, 50, 1))
CLIENT = bytes((192, 168, 50, 2))
ANY = bytes(4)
BROADCAST = bytes((255,)) * 4
COOKIE = bytes.fromhex('63825363')
LEASE, T1, T2 = 180, 60, 120


def checksum(data):
    if len(data) % 2:
        data += b'\0'
    total = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while total >> 16:
        total = (total & 0xffff) + (total >> 16)
    return (~total) & 0xffff


def options(data):
    offset = 0
    fields = {}
    while offset < len(data):
        code = data[offset]
        offset += 1
        if code == 0:
            continue
        if code == 255:
            return fields
        if offset >= len(data):
            return {}
        length = data[offset]
        offset += 1
        if offset + length > len(data) or code in fields:
            return {}
        fields[code] = data[offset:offset+length]
        offset += length
    return {}


def decode(frame):
    if (len(frame) < 282 or frame[6:12] != BOARD or
            frame[:6] not in (PEER, b'\xff' * 6) or
            frame[12:14] != b'\x08\x00' or frame[14] != 0x45 or frame[23] != 17 or
            int.from_bytes(frame[20:22], 'big') & 0x3fff):
        return None
    source, dest, length, _ = struct.unpack_from('!HHHH', frame, 34)
    if (source, dest) != (68, 67) or length < 248 or 34 + length > len(frame):
        return None
    if int.from_bytes(frame[16:18], 'big') != 20 + length:
        return None
    msg = frame[42:34+length]
    if msg[:3] != b'\x01\x01\x06' or msg[28:34] != BOARD or msg[236:240] != COOKIE:
        return None
    fields = options(msg[240:])
    kind = fields.get(53)
    if kind not in (b'\x01', b'\x03', b'\x07'):
        return None
    return kind[0], msg[4:8], msg[12:16], frame[30:34], fields


def reply(kind, xid, unicast=False):
    msg = bytearray(236)
    msg[:3] = b'\x02\x01\x06'
    msg[4:8] = xid
    msg[16:20] = CLIENT
    msg[20:24] = SERVER
    msg[28:34] = BOARD
    opts = {53: bytes((kind,)), 54: SERVER, 1: b'\xff\xff\xff\0', 3: SERVER,
            51: struct.pack('!I', LEASE), 58: struct.pack('!I', T1), 59: struct.pack('!I', T2)}
    msg = bytes(msg) + COOKIE + b''.join(bytes((k, len(v))) + v for k, v in opts.items()) + b'\xff'
    msg = msg.ljust(300, b'\0')
    udp = struct.pack('!HHHH', 67, 68, 8 + len(msg), 0)
    dest = CLIENT if unicast else BROADCAST
    ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(udp) + len(msg), 0, 0, 64, 17, 0, SERVER, dest)
    ip = ip[:10] + struct.pack('!H', checksum(ip)) + ip[12:]
    return (BOARD if unicast else b'\xff' * 6) + PEER + b'\x08\x00' + ip + udp + msg


class Scenario:
    def __init__(self):
        self.phase = 'acquire'
        self.offered = None
        self.initial_ack = None
        self.last_ack_time = None
        self.expiry_rebind_seen = False

    def handle(self, request, now):
        kind, xid, ciaddr, dest, fields = request
        if kind == 1 and ciaddr == ANY and dest == BROADCAST:
            if self.phase == 'acquire':
                self.offered = xid
                return 'discover', reply(2, xid)
            if (self.phase == 'silent' and self.expiry_rebind_seen and
                    now - self.last_ack_time >= LEASE - 60):
                self.phase = 'expired'
                return 'discover_after_expiry', None
            return 'discover_no_reply', None
        if kind == 7:
            return 'release_seen', None
        if kind != 3:
            return 'ignored', None
        if ciaddr == ANY and dest == BROADCAST and fields.get(54) == SERVER and fields.get(50) == CLIENT:
            if self.phase == 'acquire' and xid == self.offered:
                self.initial_ack = xid
                self.last_ack_time = now
                self.phase = 'renew'
                return 'initial_ack', reply(5, xid)
            if self.phase == 'renew' and xid == self.initial_ack:
                return 'initial_ack_retry', reply(5, xid)
        if ciaddr == CLIENT and dest == SERVER and 54 not in fields and 50 not in fields:
            if self.phase == 'renew':
                self.last_ack_time = now
                self.phase = 'rebind'
                return 't1_ack', reply(5, xid, unicast=True)
            return 'renew_no_reply', None
        if ciaddr == CLIENT and dest == BROADCAST and 54 not in fields and 50 not in fields:
            if self.phase == 'rebind':
                self.last_ack_time = now
                self.phase = 'silent'
                return 't2_ack', reply(5, xid)
            if self.phase == 'silent':
                self.expiry_rebind_seen = True
            return 'rebind_no_reply', None
        return 'ignored_request', None


def verify_isolated_adapter(adapter):
    # Fail closed if this is not the static test NIC or another UDP listener is bound.
    guid = adapter['InterfaceGuid'].strip('{}')
    command = (
        "$ErrorActionPreference='Stop'; "
        f"$n=Get-NetAdapter | Where-Object {{($_.InterfaceGuid -as [guid]) -eq [guid]'{guid}'}}; "
        "$p=@(Get-NetIPAddress -InterfaceIndex $n.ifIndex -AddressFamily IPv4); "
        "if ($p.Count -ne 1 -or $p[0].IPAddress -ne '192.168.50.1' -or "
        "$p[0].PrefixLength -ne 24 -or $p[0].AddressState -ne 'Preferred') {exit 1}; "
        "if (Get-NetRoute -InterfaceIndex $n.ifIndex -DestinationPrefix '0.0.0.0/0' "
        "-ErrorAction SilentlyContinue) {exit 2}; "
        "if (Get-NetUDPEndpoint -LocalPort 67 -ErrorAction SilentlyContinue | "
        "Where-Object {$_.LocalAddress -in '0.0.0.0','192.168.50.1','::'}) {exit 3}")
    if subprocess.run(['powershell.exe', '-NoProfile', '-Command', command]).returncode:
        raise RuntimeError('require only 192.168.50.1/24, no default route on test NIC, and free UDP port 67')


def board_server_frame(packet):
    """Recognize any competing server response addressed to the bench client."""
    if (len(packet) < 282 or packet[12:14] != b'\x08\x00' or
            packet[14] != 0x45 or packet[23] != 17 or
            packet[34:38] != struct.pack('!HH', 67, 68) or packet[42] != 2 or
            packet[70:76] != BOARD or packet[278:282] != COOKIE):
        return None
    return packet[:14 + int.from_bytes(packet[16:18], 'big')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--direct-board-only', action='store_true', required=True,
                        help='confirm isolated direct cable (no switch, bridge, router, or ICS)')
    parser.add_argument('--seconds', type=int, default=540, choices=range(420, 721), metavar='420..720')
    args = parser.parse_args()
    lib, handle, adapter, dll_dir = open_adapter()
    events = []
    injected = set()
    machine = Scenario()
    ended = None
    try:
        verify_isolated_adapter(adapter)
        args.output.mkdir(parents=True, exist_ok=False)
        path = args.output / 'capture.pcap'
        started = time.monotonic()
        with path.open('xb') as capture:
            capture.write(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 2048, 1))
            print('LEASE_CAPTURE_READY', flush=True)
            while time.monotonic() - started < args.seconds:
                if ended is not None and time.monotonic() - ended >= 8:
                    break
                hdr = C.POINTER(Header)()
                raw = C.POINTER(C.c_ubyte)()
                result = lib.pcap_next_ex(handle, C.byref(hdr), C.byref(raw))
                if result == 0:
                    time.sleep(0.005)
                    continue
                if result != 1:
                    raise RuntimeError('Npcap receive failed')
                h = hdr.contents
                packet = C.string_at(raw, h.caplen)
                capture.write(struct.pack('<IIII', h.seconds, h.microseconds, h.caplen, h.length))
                capture.write(packet)
                server_frame = board_server_frame(packet)
                if server_frame is not None and server_frame not in injected:
                    raise RuntimeError('competing DHCP server response captured; stopping')
                request = decode(packet)
                if request is None:
                    continue
                action, response = machine.handle(request, time.monotonic())
                if response is not None:
                    if lib.pcap_sendpacket(handle, C.create_string_buffer(response), len(response)) != 0:
                        raise RuntimeError('Npcap send failed')
                    injected.add(response)
                event = dict(action=action, xid=request[1].hex(),
                             epoch=h.seconds + h.microseconds / 1e6, replied=response is not None)
                events.append(event)
                print(json.dumps(event), flush=True)
                if action == 'discover_after_expiry':
                    ended = time.monotonic()
        stats = Stats()
        counts = None
        if lib.pcap_stats(handle, C.byref(stats)) == 0:
            counts = dict(received=stats.received, dropped=stats.dropped, interface_dropped=stats.interface_dropped)
        summary = dict(adapter=adapter, lease=LEASE, t1=T1, t2=T2, phase=machine.phase,
                       events=events, npcap_stats=counts, pcap_sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        (args.output / 'lease_peer.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        print('LEASE_CAPTURE_FINISHED ' + json.dumps(summary, ensure_ascii=True), flush=True)
    finally:
        lib.pcap_close(handle)
        dll_dir.close()


if __name__ == '__main__':
    main()
