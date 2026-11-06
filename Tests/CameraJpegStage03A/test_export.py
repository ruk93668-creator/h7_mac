"""Executable PC decoder tests use explicitly synthetic images, never board data."""
import io
import json
import tempfile
import unittest
from pathlib import Path
from PIL import Image
from verify_jpeg import read_export, validate


def record(address, kind, payload):
    raw = bytes([len(payload), address >> 8, address & 255, kind]) + payload
    return ":" + (raw + bytes([-sum(raw) & 255])).hex().upper()


class ExportTests(unittest.TestCase):
    def setUp(self):
        image = Image.new("RGB", (160, 120), (30, 90, 180))
        output = io.BytesIO()
        image.save(output, format="JPEG")
        self.data = output.getvalue()

    def test_decode(self):
        self.assertEqual(validate(self.data).size, (160, 120))

    def test_wrong_resolution(self):
        with self.assertRaises(ValueError):
            validate(self.data, 320, 240)

    def test_truncated(self):
        with self.assertRaises(ValueError):
            validate(self.data[:-1])

    def test_fake_markers(self):
        with self.assertRaises(Exception):
            validate(b"\xff\xd8fake\xff\xd9")

    def test_bin_length(self):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "synthetic.bin"
            path.write_bytes(self.data)
            self.assertEqual(read_export(path, length=len(self.data)), self.data)
            with self.assertRaises(ValueError):
                read_export(path, length=len(self.data) + 1)

    def test_hex(self):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "synthetic.hex"
            lines = [record(0, 4, bytes.fromhex("D000"))]
            lines += [record(0x400 + index, 0, self.data[index:index + 16])
                      for index in range(0, len(self.data), 16)]
            lines += [record(0, 1, b"")]
            path.write_text("\n".join(lines), encoding="ascii")
            self.assertEqual(read_export(path, length=len(self.data)), self.data)
            with self.assertRaises(ValueError):
                read_export(path, length=None)
            with self.assertRaises(ValueError):
                read_export(path, address=0xD0000401, length=len(self.data))

    def test_hex_checksum(self):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "synthetic.hex"
            path.write_text(":00000001FE", encoding="ascii")
            with self.assertRaises(ValueError):
                read_export(path, length=1)

    def test_capacity(self):
        with self.assertRaises(ValueError):
            read_export("unused.hex", length=262144)

    def test_debugger_text(self):
        with tempfile.TemporaryDirectory() as scratch:
            path = Path(scratch) / "synthetic.txt"
            lines = ["JPEG_DATA_BEGIN"]
            lines += ["JPG%08X:%s" % (i, self.data[i:i + 16].hex().upper())
                      for i in range(0, len(self.data), 16)]
            lines += ["JPEG_DATA_END"]
            path.write_text("\n".join(lines), encoding="utf-8")
            self.assertEqual(read_export(path, length=len(self.data)), self.data)
            lines.pop(2)
            path.write_text("\n".join(lines), encoding="utf-8")
            with self.assertRaises(ValueError):
                read_export(path, length=len(self.data))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ExportTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    Path(__file__).with_name("export_results.json").write_text(json.dumps({
        "total": result.testsRun, "passed": result.testsRun - len(result.failures) - len(result.errors),
        "failed": len(result.failures) + len(result.errors), "source": "synthetic",
        "hardware_verified": False}, indent=2), encoding="utf-8")
    raise SystemExit(not result.wasSuccessful())
