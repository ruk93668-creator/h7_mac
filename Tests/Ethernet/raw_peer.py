"""Linux isolated-link peer for EtherType 0x88B5. Requires raw socket permission.
Example: sudo python3 raw_peer.py --interface enp3s0 --count 1000 --seconds 10
No IP, ARP, TCP or UDP. Does not execute until explicitly launched.
"""
import argparse
import json
import socket
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--interface', required=True)
    parser.add_argument('--destination', default='02:47:47:00:00:01')
    parser.add_argument('--count', type=int, default=1)
    parser.add_argument('--seconds', type=float, default=5)
    parser.add_argument('--interval', type=float, default=0.001)
    args = parser.parse_args()
    if args.count < 0 or args.seconds <= 0 or args.interval < 0:
        parser.error('count >= 0, seconds > 0, interval >= 0 required')
    destination = bytes.fromhex(args.destination.replace(':', ''))
    if len(destination) != 6:
        parser.error('destination must contain six MAC bytes')
    with socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(0x88B5)) as sock:
        sock.bind((args.interface, 0))
        source = sock.getsockname()[4]
        sock.settimeout(0.001)
        sent = received = received_bytes = 0
        first_sequence = last_sequence = None
        start = next_send = time.monotonic()
        while time.monotonic() - start < args.seconds:
            now = time.monotonic()
            if sent < args.count and now >= next_send:
                frame = destination + source + b'\x88\xb5' + struct.pack('!I', sent) + b'\xa5' * 42
                sock.send(frame)
                sent += 1
                next_send = now + args.interval
            try:
                frame, address = sock.recvfrom(2048)
            except socket.timeout:
                continue
            if address[2] == socket.PACKET_OUTGOING:
                continue
            if len(frame) >= 18 and frame[12:14] == b'\x88\xb5':
                received += 1
                received_bytes += len(frame)
                last_sequence = struct.unpack('!I', frame[14:18])[0]
                if first_sequence is None:
                    first_sequence = last_sequence
        print(json.dumps(dict(sent=sent, received=received, received_bytes=received_bytes,
                              elapsed_seconds=time.monotonic()-start,
                              first_sequence=first_sequence, last_sequence=last_sequence), indent=2))


if __name__ == '__main__':
    main()
