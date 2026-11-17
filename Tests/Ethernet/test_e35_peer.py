import unittest
from pathlib import Path
import subprocess
from e35_peer import frame, inspect


class ProtocolTests(unittest.TestCase):
    def test_real_c_vectors(self):
        executable = Path(__file__).parent / 'build/test_e35_frame.exe'
        lines = subprocess.check_output([str(executable), '--vectors'], text=True).splitlines()
        self.assertEqual(len(lines), 10)
        for line in lines:
            length, direction, encoded = line.split()
            length = int(length)
            self.assertEqual(bytes.fromhex(encoded), frame(0x12345678, 257, length, length >= 1518, bool(int(direction))))

    def test_lengths_and_corruption(self):
        for length, vlan in [(60, False), (512, False), (1514, False), (1518, True), (1522, True)]:
            packet = frame(0x12345678, 257, length, vlan, True)
            self.assertEqual(len(packet), length)
            self.assertEqual(inspect(packet, 0x12345678, length, vlan), 257)
            self.assertIsNone(inspect(packet, 1, length, vlan))
            self.assertIsNone(inspect(frame(1, 1, length, vlan), 1, length, vlan))
            self.assertEqual(inspect(frame(1, 1, length, vlan), 1, length, vlan, False), 1)
            with self.assertRaises(ValueError):
                inspect(packet[:-1] + bytes([packet[-1] ^ 1]), 0x12345678, length, vlan)

    def test_golden_layout(self):
        packet = frame(0x12345678, 257, 60)
        self.assertEqual(packet[:28].hex(), '024747000001ec1ac33edbb288b5453335211234567800000101003c')
        self.assertEqual(packet[28], (28 ^ 257 ^ 1 ^ 0x12345678) & 255)
        self.assertIsNone(inspect(b'', 1, 60, False))
        with self.assertRaises(ValueError):
            frame(0, 1, 60)


if __name__ == '__main__':
    unittest.main()
