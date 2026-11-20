"""Exercise the E6 HTTP/MJPEG firmware on an isolated board network."""
import argparse
import http.client
import json
import os
from pathlib import Path
import re
import socket
import struct
import time
import zlib


def require(condition, message):
    if not condition:
        raise ValueError(message)


def jpeg_size(data):
    require(data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9', 'missing JPEG SOI or EOI')
    offset = 2
    while offset + 4 <= len(data):
        require(data[offset] == 255, 'invalid JPEG marker')
        kind = data[offset + 1]
        length = int.from_bytes(data[offset + 2:offset + 4], 'big')
        require(length >= 2 and offset + length + 2 <= len(data), 'invalid JPEG segment length')
        if kind == 0xc0:
            require(length >= 8, 'invalid JPEG frame segment')
            height, width = struct.unpack_from('!HH', data, offset + 5)
            return width, height
        require(kind not in (0xda, 0xd9), 'JPEG frame header missing')
        offset += length + 2
    raise ValueError('JPEG frame header missing')


def read_frame(stream, expected):
    require(stream.readline(64) == b'--h7frame\r\n', 'invalid multipart boundary')
    fields = {}
    for _ in range(8):
        line = stream.readline(128)
        if line == b'\r\n':
            break
        name, sep, value = line.rstrip(b'\r\n').partition(b':')
        name = name.lower()
        require(line.endswith(b'\r\n') and sep and name not in fields, 'invalid multipart field')
        fields[name] = value.strip()
    else:
        raise ValueError('too many multipart fields')
    require(fields.get(b'content-type') == b'image/jpeg', 'invalid part Content-Type')
    size = fields.get(b'content-length', b'')
    seq = fields.get(b'x-frame-sequence', b'')
    crc = fields.get(b'x-frame-crc32', b'')
    require(size.isdigit() and len(size) <= 5 and seq.isdigit() and len(seq) <= 10 and
            re.fullmatch(rb'[0-9a-fA-F]{8}', crc), 'invalid length, sequence or CRC field')
    require(4 <= int(size) <= 65535 and int(seq) == expected, 'invalid length or frame sequence')
    data = stream.read(int(size))
    require(len(data) == int(size) and stream.read(2) == b'\r\n', 'incomplete JPEG or trailer')
    require(zlib.crc32(data) == int(crc, 16), 'JPEG CRC mismatch')
    require(jpeg_size(data) == (160, 120), 'unexpected JPEG dimensions')
    return len(data), crc.decode('ascii')


def connect(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.settimeout(args.timeout)
        if args.receive_buffer:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, args.receive_buffer)
        if args.local_ip:
            sock.bind((args.local_ip, 0))
        sock.connect((args.board_ip, args.port))
        return sock
    except BaseException:
        sock.close()
        raise


def request(sock, host, path, extra=b''):
    sock.sendall(b'GET ' + path + b' HTTP/1.1\r\nHost: ' + host.encode('ascii') +
                 b'\r\nConnection: close\r\n' + extra + b'\r\n')
    response = http.client.HTTPResponse(sock, method='GET')
    response.begin()
    return response


def routes(args):
    outcomes = {}
    for path, expected, extra in ((b'/', 200, b''), (b'/health', 200, b''),
                                  (b'/favicon.ico', 204, b''), (b'/missing', 404, b''),
                                  (b'/', 431, b'X-Long: ' + b'x' * 512 + b'\r\n')):
        with connect(args) as sock:
            with request(sock, args.board_ip, path, extra) as response:
                require(response.status == expected, f'{path!r}: expected {expected}, got {response.status}')
                body = response.read(2048)
                if expected == 200 and path == b'/':
                    require(b'STM32H747 Ethernet Camera' in body and b'src="/stream.mjpg"' in body and
                            args.board_ip.encode('ascii') in body, 'incomplete homepage')
                if path == b'/health':
                    require(body == b'ok\n', 'invalid health response')
                if body and response.headers.get('Content-Length'):
                    require(len(body) == int(response.headers['Content-Length']), 'incomplete HTTP body')
            require(not sock.recv(1), 'expected server FIN')
        outcomes[str(expected) + ' ' + path.decode()] = expected
        time.sleep(.05)
    return outcomes


def measure(args):
    count = bytes_received = 0
    first = last = None
    checksums = set()
    with connect(args) as sock:
        with request(sock, args.board_ip, b'/stream.mjpg') as response:
            require(response.status == 200 and response.headers.get('Content-Type') ==
                    'multipart/x-mixed-replace; boundary=h7frame', 'invalid MJPEG response')
            require(response.headers.get('Content-Length') is None, 'unexpected stream Content-Length')
            while count < args.frames or (args.seconds and (first is None or last - first < args.seconds)):
                length, crc = read_frame(response, count + 1)
                last = time.monotonic()
                if first is None:
                    first = last
                count += 1
                bytes_received += length
                checksums.add(crc)
                if args.slow_ms:
                    time.sleep(args.slow_ms / 1000)
            if args.stop_read_seconds:
                time.sleep(args.stop_read_seconds)
            if args.rst:
                linger = 'HH' if os.name == 'nt' else 'ii'
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack(linger, 1, 0))
                shutdown = 'rst_requested'
            else:
                sock.shutdown(socket.SHUT_WR)
                deadline = time.monotonic() + args.timeout
                # After FIN a deliberately partial final part can still arrive from the send ring.
                while True:
                    left = deadline - time.monotonic()
                    require(left > 0, 'server did not finish after FIN')
                    sock.settimeout(left)
                    if not response.read(4096):
                        break
                shutdown = 'fin_received'
    elapsed = last - first
    fps = (count - 1) / elapsed if elapsed else 0.0
    require(fps >= args.min_fps, f'FPS below {args.min_fps}: {fps:.3f}')
    require(count < 8 or len(checksums) >= 8, 'stream did not show all eight different fixtures')
    return dict(frames=count, jpeg_bytes=bytes_received, seconds=elapsed, fps=fps,
                jpeg_mbps=8 * bytes_received / elapsed / 1e6 if elapsed else 0,
                unique_frames=len(checksums), shutdown=shutdown)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--board-ip', required=True)
    parser.add_argument('--local-ip')
    parser.add_argument('--port', type=int, default=80)
    parser.add_argument('--frames', type=int, default=10)
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--min-fps', type=float, default=0)
    parser.add_argument('--timeout', type=float, default=8)
    parser.add_argument('--reconnect', type=int, default=1)
    parser.add_argument('--slow-ms', type=int, default=0)
    parser.add_argument('--stop-read-seconds', type=float, default=0)
    parser.add_argument('--receive-buffer', type=int, default=0)
    parser.add_argument('--rst', action='store_true')
    parser.add_argument('--stream-only', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if (args.frames < 1 or args.reconnect < 1 or not 1 <= args.port <= 65535 or args.timeout <= 0 or
            any(value < 0 for value in (args.seconds, args.min_fps, args.slow_ms,
                                        args.stop_read_seconds, args.receive_buffer))):
        parser.error('invalid test limits')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result = dict(status='FAIL', routes={}, sessions=[])
    with args.output.open('x', encoding='utf-8') as output:
        try:
            if not args.stream_only:
                result['routes'] = routes(args)
            for _ in range(args.reconnect):
                result['sessions'].append(measure(args))
                time.sleep(.05)
            result['status'] = 'PASS'
        except (OSError, ValueError, http.client.HTTPException) as exc:
            result['error'] = str(exc)
        json.dump(result, output, indent=2)
    print(json.dumps(result))
    return int(result['status'] != 'PASS')


if __name__ == '__main__':
    raise SystemExit(main())
