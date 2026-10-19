import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
NOW=1800000000

def varint(n):
    out=bytearray()
    while n>127: out.append((n&127)|128); n>>=7
    return bytes(out)+bytes([n])
def number(tag,n):return varint(tag<<3)+varint(n)
def blob(tag,s):
    if isinstance(s,str):s=s.encode()
    return varint(tag<<3|2)+varint(len(s))+s
def translated(s):return blob(1,blob(1,s)+blob(2,'en'))
def feed(payload=b'',trips=False,stamp=NOW,incremental=0):
    h=blob(1,'2.0')+number(2,incremental)+number(3,stamp)
    return blob(1,h)+(blob(2,blob(1,'fixture-alert')+blob(3 if trips else 5,payload)) if payload else b'')
class Feed(C.Structure):
    _fields_=[('text',C.c_void_p),('length',C.c_size_t),('alerts',C.c_uint),('timestamp',C.c_uint64),('headline',C.c_char*160)]
class TrainTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();lib=Path(cls.tmp.name)/'train.so'
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fPIC','-shared','-I'+str(ROOT/'main'),str(ROOT/'main/passport_train.c'),'-o',str(lib)],check=True)
        cls.lib=C.CDLL(str(lib));cls.lib.passport_train_decode.argtypes=[C.c_void_p,C.c_size_t,C.c_bool,C.c_uint64,C.POINTER(Feed)]
        cls.lib.passport_train_free.argtypes=[C.POINTER(Feed)]
        cls.lib.passport_train_download_url.argtypes=[C.c_char_p];cls.lib.passport_train_download_url.restype=C.c_bool
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def decode(self,data,trips=False):
        f=Feed();ok=bool(self.lib.passport_train_decode(data,len(data),trips,NOW,C.byref(f)))
        text=C.string_at(f.text).decode() if f.text else ''
        result=(ok,f.alerts,text,bytes(f.headline).decode());self.lib.passport_train_free(C.byref(f));return result
    def test_full_alert_and_long_description(self):
        description='Signal fault. '*100+'终点：请转乘巴士。'
        a=blob(10,translated('EWL disruption'))+blob(11,translated(description))+number(6,3)+number(7,1)+blob(5,blob(2,'EWL')+blob(5,'EW7'))
        ok,n,text,headline=self.decode(feed(a));self.assertTrue(ok);self.assertEqual(n,1);self.assertIn(description,text);self.assertIn('Route: EWL',text);self.assertEqual(headline,'EWL disruption')
    def test_empty_is_clear_but_failed_feed_is_not(self):
        self.assertEqual(self.decode(feed())[:2],(True,0))
        for data in [b'',b'{}',b'\x0a\xff',b'\x0a\x02\x0a\x80',b'\x80'*20]:self.assertFalse(self.decode(data)[0])
        self.assertTrue(self.decode(feed(stamp=NOW-901))[0]);self.assertFalse(self.decode(feed(incremental=1))[0])
    def test_active_periods_and_deleted_entities(self):
        for start,end,count in [(NOW-10,NOW+10,1),(NOW+10,NOW+20,0),(NOW-20,NOW,0)]:
            a=blob(1,number(1,start)+number(2,end))+blob(10,translated('test'))
            self.assertEqual(self.decode(feed(a))[1],count)
        a=blob(10,translated('deleted'))
        data=blob(1,blob(1,'2.0')+number(3,NOW))+blob(2,blob(1,'a')+number(2,1)+blob(5,a))
        self.assertEqual(self.decode(data)[:2],(True,0))
    def test_trip_disruption_delay_cancellation_and_stop(self):
        trip=blob(1,blob(1,'trip-42')+blob(5,'NSL')+number(4,3))+number(5,600)+blob(2,blob(4,'NS1')+number(5,1)+blob(2,number(1,300)+number(2,NOW+300)))
        ok,n,text,_=self.decode(feed(trip,True),True);self.assertTrue(ok);self.assertEqual(n,1);self.assertIn('Delay seconds: 600',text);self.assertIn('Stop: NS1',text);self.assertIn('Cancelled (3)',text)
    def test_unknown_fields_truncation_and_size_bound(self):
        a=blob(10,translated('test'))+blob(999,'unknown')
        good=feed(a);self.assertTrue(self.decode(good)[0])
        for n in range(1,len(good)):
            if n!=len(feed()):self.assertFalse(self.decode(good[:n])[0])
        self.assertFalse(self.decode(feed(blob(11,translated('x'*33000))))[0])
        self.assertFalse(self.decode(b'x'*65537)[0])
    def test_download_host_and_path_allowlist(self):
        base='https://dmprod-datasets.s3.ap-southeast-1.amazonaws.com/'
        for path in ['train-gtfs-real-time/gtfs_realtime.pb?X-Amz-Signature=test','train-gtfs-trip-update/gtfs_trip_update.pb']:
            self.assertTrue(self.lib.passport_train_download_url((base+path).encode()))
        for u in ['http://example.com','https://example.com/train-gtfs-real-time/f.pb',base+'other/data',base+'@evil.com/',base+'train-gtfs-real-time/\n']:
            self.assertFalse(self.lib.passport_train_download_url(u.encode()))
if __name__=='__main__':unittest.main()
