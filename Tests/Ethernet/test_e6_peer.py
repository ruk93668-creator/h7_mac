from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from contextlib import redirect_stderr, redirect_stdout
from io import BytesIO, StringIO
from pathlib import Path
import json
import select
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zlib

import e6_peer as peer


QVGA = Path(__file__).with_name('e6_peer_fixtures').joinpath('qvga.jpg').read_bytes()
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
                for index, frame in enumerate(getattr(self.server, 'frames', FIXTURES), 1):
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

    def test_qvga_frame(self):
        validation = peer.jpeg_validation()
        self.assertEqual(peer.jpeg_size(QVGA), (320, 240))
        self.assertEqual(peer.read_frame(BytesIO(part(QVGA, 1)), 1, 320, 240, validation),
                         (len(QVGA), f'{zlib.crc32(QVGA):08X}'))
        self.assertEqual(validation, dict(status='PASS', frames_checked=1, crc_verified_frames=1,
                                         observed_dimensions=[dict(width=320, height=240)]))

    def test_dimension_mismatch_both_directions(self):
        for frame, width, height, actual in ((FIXTURES[0], 320, 240, (160, 120)),
                                             (QVGA, 160, 120, (320, 240))):
            with self.subTest(expected=(width, height)):
                validation = peer.jpeg_validation()
                with self.assertRaisesRegex(ValueError, 'unexpected JPEG dimensions'):
                    peer.read_frame(BytesIO(part(frame, 1)), 1, width, height, validation)
                self.assertEqual(validation['status'], 'FAIL')
                self.assertEqual(validation['frames_checked'], 0)
                self.assertEqual(validation['crc_verified_frames'], 1)
                self.assertEqual(validation['observed_dimensions'],
                                 [dict(width=actual[0], height=actual[1])])

    def test_corrupt_payload_retains_crc_assertion(self):
        data = bytearray(part(QVGA, 1))
        data[-8] ^= 1
        validation = peer.jpeg_validation()
        with self.assertRaisesRegex(ValueError, 'JPEG CRC mismatch'):
            peer.read_frame(BytesIO(data), 1, 320, 240, validation)
        self.assertEqual(validation['status'], 'FAIL')
        self.assertEqual(validation['crc_verified_frames'], 0)

    def test_invalid_sof_and_lengths_with_matching_crc(self):
        sof = QVGA.index(b'\xff\xc0')
        cases = {}
        for name, relative, value in (('short_sof', 2, b'\x00\x07'),
                                       ('oversized_segment', 2, b'\xff\xff'),
                                       ('zero_width', 7, b'\x00\x00'),
                                       ('zero_height', 5, b'\x00\x00'),
                                       ('zero_components', 9, b'\x00'),
                                       ('missing_component_records', 9, b'\x04'),
                                       ('missing_sof', 1, b'\xc4')):
            frame = bytearray(QVGA)
            frame[sof + relative:sof + relative + len(value)] = value
            cases[name] = bytes(frame)
        cases['missing_eoi'] = QVGA[:-2]
        cases['truncated_sof'] = QVGA[:sof + 8] + b'\xff\xd9'
        for name, frame in cases.items():
            with self.subTest(case=name):
                validation = peer.jpeg_validation()
                with self.assertRaises(ValueError):
                    peer.read_frame(BytesIO(part(frame, 1)), 1, 320, 240, validation)
                self.assertEqual(validation['status'], 'FAIL')
                self.assertEqual(validation['frames_checked'], 0)

    def test_cli_json_defaults_qvga_and_mismatches(self):
        with ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server, tempfile.TemporaryDirectory() as tmp:
            worker = threading.Thread(target=server.serve_forever, daemon=True)
            worker.start()
            try:
                cases = [('legacy', FIXTURES, [], (160, 120), 0),
                         ('qvga', [QVGA] * 8, ['--width', '320', '--height', '240'], (320, 240), 0),
                         ('small_mismatch', FIXTURES, ['--width', '320', '--height', '240'], (320, 240), 1),
                         ('large_mismatch', [QVGA] * 8, [], (160, 120), 1)]
                for name, frames, options, expected, code in cases:
                    with self.subTest(case=name):
                        server.frames = frames
                        out = Path(tmp) / (name + '.json')
                        argv = ['e6_peer.py', '--board-ip', '127.0.0.1', '--port', str(server.server_port),
                                '--frames', '8', '--slow-ms', '5', '--output', str(out), *options,
                                *(['--camera'] if name != 'legacy' else [])]
                        with patch('sys.argv', argv), redirect_stdout(StringIO()):
                            self.assertEqual(peer.main(), code)
                        result = json.loads(out.read_text())
                        self.assertEqual(result['status'], 'FAIL' if code else 'PASS')
                        self.assertEqual(result['expected_resolution'], dict(width=expected[0], height=expected[1]))
                        check = result['jpeg_validation'][0]
                        self.assertEqual(check['status'], result['status'])
                        if not code:
                            session = result['sessions'][0]
                            self.assertEqual(session['frames'], 8)
                            self.assertEqual(session['jpeg_bytes'], sum(map(len, frames)))
                            self.assertGreater(session['fps'], 0)
                            self.assertEqual(check['frames_checked'], 8)
                            self.assertEqual(check['crc_verified_frames'], 8)
                            self.assertEqual(check['observed_dimensions'], [result['expected_resolution']])
                        else:
                            self.assertEqual(check['frames_checked'], 0)
                            self.assertIn('unexpected JPEG dimensions', result['error'])
            finally:
                server.shutdown()
                worker.join()

    def test_cli_invalid_resolution_rejected_before_network(self):
        for option in ('--width', '--height'):
            for value in ('0', '-1', '65536', '1.5', 'invalid'):
                with self.subTest(option=option, value=value), tempfile.TemporaryDirectory() as tmp:
                    out = Path(tmp) / 'invalid.json'
                    argv = ['e6_peer.py', '--board-ip', '127.0.0.1', '--output', str(out), option, value]
                    with patch('sys.argv', argv), patch.object(peer, 'connect') as connect, redirect_stderr(StringIO()):
                        with self.assertRaises(SystemExit) as raised:
                            peer.main()
                        self.assertEqual(raised.exception.code, 2)
                        connect.assert_not_called()
                        self.assertFalse(out.exists())

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
