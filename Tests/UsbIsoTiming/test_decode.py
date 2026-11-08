import unittest,struct,tempfile
from pathlib import Path
from decode_trace import decode,load,BASE
class DecodeTests(unittest.TestCase):
    def meta(self,count=6,state=3):return struct.pack('<16I',1,0,state,count,512,48,BASE,24576,1000000,0,0,0,1000,1,0x5554494D,2)
    def raw(self):
        events=[(10,3,1),(20,4,0),(30,5,0),(50,1,1),(60,2,1),(70,3,2)]
        return b''.join(struct.pack('<12I',t,1,k,seq,0,0,0,256,0,0,t,1) for t,k,seq in events)
    def test_metrics(self):
        r,_=decode(self.raw(),self.meta());self.assertEqual(r['metrics']['submit_to_hal_enter']['mean_us'],10);self.assertEqual(r['metrics']['hal_enter_to_exit']['mean_us'],10);self.assertEqual(r['metrics']['datain_to_confirm']['mean_us'],10);self.assertEqual(r['metrics']['submit_to_datain']['mean_us'],40)
    def test_not_ready(self):
        with self.assertRaises(ValueError):decode(self.raw(),self.meta(state=1))
    def test_truncated(self):
        with self.assertRaises(ValueError):decode(self.raw()[:-1],self.meta())
    def test_uncommitted(self):
        with self.assertRaises(ValueError):decode(bytes(288),self.meta())
    def test_count_limit(self):
        with self.assertRaises(ValueError):decode(bytes(513*48),self.meta(count=513))
    def test_wrap(self):
        start=0xfffffff0;meta=list(struct.unpack('<16I',self.meta(count=1)));meta[11]=start
        raw=struct.pack('<12I',16,1,3,1,0,0,0,0,0,0,16,1);_,rows=decode(raw,struct.pack('<16I',*meta));self.assertEqual(rows[0]['dwt_relative_us'],32)
    def test_invalid_wall_not_used(self):
        raw=bytearray(self.raw());struct.pack_into('<I',raw,44,8);r,_=decode(raw,self.meta());self.assertEqual(r['invalid_wall_records'],1);self.assertEqual(r['metrics']['submit_to_datain']['samples'],0)
    def test_hex_checksum(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'bad.hex';p.write_text(':0100000000FE\n')
            with self.assertRaises(ValueError):load(p)
if __name__=='__main__':unittest.main()
