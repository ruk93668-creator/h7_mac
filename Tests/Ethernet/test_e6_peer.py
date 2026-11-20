from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
from pathlib import Path
import select
import threading
from types import SimpleNamespace
import unittest
import zlib

import e6_peer as peer


FIXTURES = [path.read_bytes() for path in sorted(Path(__file__).with_name('e6_jpeg_fixtures').glob('*.jpg'))]


def part(frame, sequence):
    return (f'--h7frame\r\nContent-Type: image/jpeg\r\nContent-Length: {len(frame)}\r\n'
            f'X-Frame-Sequence: {sequence}\r\nX-Frame-CRC32: {zlib.crc32(frame):08X}\r\n\r\n'.encode()
            + frame + b'\r\n')


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path == '/stream.mjpg':
            self.send_response(200)
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=h7frame')
            self.send_header('Connection', 'close')
            self.end_headers()
            try:
                for index, frame in enumerate(FIXTURES, 1):
                    self.wfile.write(part(frame, index))
                if select.select([self.connection], [], [], 2)[0]:
                    self.connection.recv(1)
            except OSError:
                pass
            return
        code, body = {
            '/': (200, b'<title>STM32H747 Ethernet Camera</title>127.0.0.1<img src="/stream.mjpg">'),
            '/health': (200, b'ok\n'), '/favicon.ico': (204, b'')
        }.get(self.path, (404, b'not found'))
        if len(self.headers.get('X-Long', '')) >= 512:
            code, body = 431, b'header too large'
        self.send_response(code)
        self.send_header('Connection', 'close')
        if code != 204:
            self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class TestE6Peer(unittest.TestCase):
    def test_fixtures_and_multiple_parts(self):
        self.assertEqual(len(set(FIXTURES)), 8)
        stream = BytesIO(b''.join(part(frame, i) for i, frame in enumerate(FIXTURES, 1)))
        for i, frame in enumerate(FIXTURES, 1):
            self.assertEqual(peer.jpeg_size(frame), (160, 120))
            self.assertEqual(peer.read_frame(stream, i), (len(frame), f'{zlib.crc32(frame):08X}'))
        self.assertEqual(stream.read(), b'')

    def test_rejects_invalid_part(self):
        frame = FIXTURES[0]
        valid = part(frame, 1)
        length = f'Content-Length: {len(frame)}'.encode()
        crc = f'X-Frame-CRC32: {zlib.crc32(frame):08X}'.encode()
        for data in (valid[1:], valid[:-4], valid.replace(length, length + b'0'),
                     valid.replace(b'Sequence: 1', b'Sequence: 2'), valid.replace(crc, b'X-Frame-CRC32: 00000000'),
                     part(frame[:-2] + b'\0\0', 1), part(b'\0\0' + frame[2:], 1)):
            with self.subTest(prefix=data[:60]):
                with self.assertRaises(ValueError):
                    peer.read_frame(BytesIO(data), 1)

    def test_real_socket_client_fin_reset_reconnect(self):
        with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                args = SimpleNamespace(board_ip='127.0.0.1', local_ip=None, port=server.server_port,
                                       timeout=3, receive_buffer=4096, frames=8, seconds=0, min_fps=0,
                                       slow_ms=2, stop_read_seconds=.05, rst=False)
                self.assertEqual(set(peer.routes(args).values()), {200, 204, 404, 431})
                self.assertEqual(peer.measure(args)['shutdown'], 'fin_received')
                args.rst = True
                self.assertEqual(peer.measure(args)['shutdown'], 'rst_requested')
                args.rst = False
                self.assertEqual(peer.measure(args)['frames'], 8)
            finally:
                server.shutdown()
                worker.join()


if __name__ == '__main__':
    unittest.main()
