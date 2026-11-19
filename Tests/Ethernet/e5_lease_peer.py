"""E5 isolated-link DHCP responder which keeps renewing the existing short lease."""
import argparse
import ctypes as C
import json
from pathlib import Path
import time

from e35_peer import Header, Stats, open_adapter
from e42_lease_peer import (CLIENT, SERVER, BROADCAST, LEASE, T1, T2, Scenario,
                            board_server_frame, decode, reply, verify_isolated_adapter)


class ContinuousLease(Scenario):
    def handle(self, request, now):
        kind, xid, ciaddr, dest, fields = request
        if self.phase == 'renew' and kind == 3 and ciaddr == CLIENT and 54 not in fields and 50 not in fields:
            if dest == SERVER:
                return 't1_ack', reply(5, xid, unicast=True)
            if dest == BROADCAST:
                return 't2_ack', reply(5, xid)
        return super().handle(request, now)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--direct-board-only', required=True, action='store_true')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--seconds', type=int, default=1200)
    args = parser.parse_args()
    if not 60 <= args.seconds <= 1800:
        parser.error('duration must be 60..1800 seconds')

    lib, handle, adapter, dll_dir = open_adapter()
    machine = ContinuousLease()
    events = []
    injected = set()
    status = 'interrupted'
    try:
        verify_isolated_adapter(adapter)
        log = args.output.open('x', encoding='utf-8', buffering=1)
        with log:
            print('LEASE_READY', flush=True)
            start = time.monotonic()
            stop = args.output.with_suffix('.stop')
            while time.monotonic() - start < args.seconds and not stop.exists():
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
                server_frame = board_server_frame(packet)
                if server_frame is not None and server_frame not in injected:
                    raise RuntimeError('competing DHCP server response captured')
                request = decode(packet)
                if request is None:
                    continue
                action, response = machine.handle(request, time.monotonic())
                if response is not None:
                    if lib.pcap_sendpacket(handle, C.create_string_buffer(response), len(response)) != 0:
                        raise RuntimeError('Npcap send failed')
                    injected.add(response)
                event = dict(action=action, xid=request[1].hex(), epoch=h.seconds + h.microseconds / 1e6,
                             replied=response is not None)
                events.append(event)
                log.write(json.dumps(event) + '\n')
                print(json.dumps(event), flush=True)
            status = 'stopped' if stop.exists() else 'timeout'
    finally:
        stats = Stats()
        counts = {}
        if lib.pcap_stats(handle, C.byref(stats)) == 0:
            counts = dict(received=stats.received, dropped=stats.dropped, interface_dropped=stats.interface_dropped)
        lib.pcap_close(handle)
        dll_dir.close()
        if 'log' in locals():
            summary = dict(status=status, lease=LEASE, t1=T1, t2=T2, events=events, npcap=counts)
            with args.output.with_suffix('.json').open('x', encoding='utf-8') as output:
                json.dump(summary, output, indent=2)


if __name__ == '__main__':
    main()
