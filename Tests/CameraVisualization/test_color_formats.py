"""Synthetic decoder vectors only; these do not validate hardware capture."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from PIL import Image
import view_frame as tool

HERE = Path(__file__).resolve().parent

class ColorChecks(unittest.TestCase):
    def test_rgb565_little_endian_primaries(self):
        raw = struct.pack('<6H', 0xF800, 0x07E0, 0x001F, 0xFFFF, 0, 0x8410)
        pixels, channels, luma = tool.decode_pixels(raw, 'rgb565', 6, 1)
        self.assertEqual(channels, 3)
        self.assertEqual(pixels, bytes([255,0,0, 0,255,0, 0,0,255, 255,255,255, 0,0,0, 132,130,132]))
        self.assertEqual(len(luma), 6)

    def test_yuyv_limited_neutral_pair_order(self):
        pixels, channels, luma = tool.decode_pixels(bytes([16,128,235,128]), 'yuv422', 2, 1)
        self.assertEqual(pixels, bytes([0,0,0,255,255,255]))
        self.assertEqual(luma, bytes([16,235]))
        self.assertEqual(channels, 3)

    def test_yuyv_limited_red(self):
        pixels, _, _ = tool.decode_pixels(bytes([81,90,81,240]), 'yuyv', 2, 1)
        self.assertEqual(pixels, bytes([255,0,0])*2)

    def test_yuyv_full_range(self):
        pixels, _, _ = tool.decode_pixels(bytes([0,128,255,128]), 'yuyv', 2, 1, 'full')
        self.assertEqual(pixels, bytes([0,0,0,255,255,255]))

    def test_invalid_decode_inputs(self):
        for fmt, data, width, height in [('unknown',b'',2,1),('yuv422',b'123456',3,1),
                                          ('rgb565',b'123',2,1),('y8',b'',0,1)]:
            with self.assertRaises(ValueError): tool.decode_pixels(data,fmt,width,height)
        with self.assertRaises(ValueError): tool.decode_pixels(b'1234','yuv422',2,1,'auto')

    def test_rgb_png_preview_channels_and_rows(self):
        pixels = bytes([255,0,0,0,255,0, 0,0,255,255,255,255])
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'rgb.png'; tool.write_png(p,pixels,2,2,3)
            with Image.open(p) as image:
                self.assertEqual(image.mode,'RGB'); self.assertEqual(image.tobytes(),pixels)
            enlarged=tool.enlarge_nearest(pixels,2,2,channels=3)
            tool.write_png(p,enlarged,8,8,3)
            with Image.open(p) as image:
                for y in range(8):
                    for x in range(8):
                        i=((y//4)*2+x//4)*3
                        self.assertEqual(image.getpixel((x,y)),tuple(pixels[i:i+3]))

    def test_convert_all_formats_preserves_raw_and_crc(self):
        for fmt, raw in [('y8',bytes([70])*19200), ('rgb565',b'\x00\xf8'*19200),
                         ('yuv422',bytes([16,128,235,128])*9600)]:
            with self.subTest(format=fmt), tempfile.TemporaryDirectory() as temp:
                p=Path(temp)/'synthetic.bin';p.write_bytes(raw)
                r=tool.convert(p,Path(temp)/'out',zlib.crc32(raw),fmt)
                out=Path(r['output_path'])
                self.assertEqual((out/'frame.bin').read_bytes(),raw)
                self.assertEqual((out/'source.bin').read_bytes(),raw)
                self.assertTrue(r['watch_crc_matches'])
                self.assertEqual(r['metrics']['size_bytes'],len(raw))
                self.assertEqual(r['metrics']['stride_bytes'],160*(1 if fmt=='y8' else 2))
                expected, channels, _=tool.decode_pixels(raw,fmt)
                with Image.open(out/'frame_160x120.png') as image:
                    self.assertEqual(image.mode,'L' if channels==1 else 'RGB')
                    self.assertEqual(image.tobytes(),expected)
                self.assertEqual(r['visual_review_status'],'PENDING')

    def test_color_length_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'short.bin';p.write_bytes(b'\0'*19200)
            with self.assertRaisesRegex(ValueError,'exactly 38400'):
                tool.convert(p,Path(temp)/'out',pixel_format='rgb565')
            self.assertFalse((Path(temp)/'out').exists())

    def test_color_hex_complete_range_and_missing_tail(self):
        def record(address,kind,data):
            b=bytes([len(data),address>>8,address&255,kind])+data
            return ':'+(b+bytes([(-sum(b))&255])).hex().upper()
        raw=b'\xe0\x07'*19200
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'synthetic.hex'
            lines=[record(0,4,bytes([0xD0,0]))]
            lines += [record(0x400+i,0,raw[i:i+16]) for i in range(0,len(raw),16)]
            lines += [record(0,1,b'')]
            p.write_text('\n'.join(lines),encoding='ascii')
            r=tool.convert(p,Path(temp)/'out',pixel_format='rgb565')
            self.assertEqual(r['source_info']['selected_end'],'0xD00099FF')
            self.assertEqual((Path(r['output_path'])/'frame.bin').read_bytes(),raw)
            del lines[-2];p.write_text('\n'.join(lines),encoding='ascii')
            with self.assertRaisesRegex(ValueError,'Missing 16 bytes'):
                tool.convert(p,Path(temp)/'out',pixel_format='rgb565')

    def test_real_y8_convert_backward_compatibility(self):
        source=HERE/'captures/20261020_160039_979921/source.hex'
        with tempfile.TemporaryDirectory() as temp:
            r=tool.convert(source,Path(temp),0x35A2952B)
            out=Path(r['output_path'])
            old=source.parent
            for name in ['frame.bin','frame_160x120.png','frame_preview_4x.png']:
                self.assertEqual((out/name).read_bytes(),(old/name).read_bytes())

    def test_cli_and_crc_mismatch(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'synthetic.bin';p.write_bytes(b'\x00\xf8'*19200)
            cmd=[sys.executable,str(HERE/'view_frame.py'),str(p),'--format','rgb565','--out',str(Path(temp)/'out')]
            result=subprocess.run(cmd,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run(cmd+['--expected-crc','0'],capture_output=True)
            self.assertEqual(result.returncode,2,result.stderr)

if __name__ == '__main__':
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ColorChecks))
    (HERE/'color_tool_checks.json').write_text(json.dumps({'tests':result.testsRun,'failures':len(result.failures),'errors':len(result.errors),'all_passed':result.wasSuccessful(),'note':'Synthetic decoder unit vectors and historical real Y8 roundtrip only; no new real RGB565/YUV422 capture acceptance.'},indent=2),encoding='utf-8')
    raise SystemExit(0 if result.wasSuccessful() else 1)
