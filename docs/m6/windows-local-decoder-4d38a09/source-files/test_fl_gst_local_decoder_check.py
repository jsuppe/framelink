#!/usr/bin/env python3
import csv,json,pathlib,tempfile,unittest
from fl_gst_local_decoder_check import check
class DecoderCheck(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=pathlib.Path(self.tmp.name)
  for role in ('source','receiver'):
   rows=[]
   for n in range(16):
    pts=n*10**9//30;offset=10**9 if role=='receiver' else 0
    rows.append(dict(identity=n,decoded_pts_ns=100*10**9+pts,relative_pts_ns=pts,duration_ns=10**9//30,hash=n+100,video_pts_ns=37000000+pts,pcm_pts_ns=pts+offset,generation=23,metadata_version=1,metadata_flags=2,dts_ns=0,oracle_readback_bytes=16384 if role=='receiver' and n==15 else 8192,mapped_video_pts_ns=37000000+pts+offset,decoded_dts_ns=2**64-1,converted_output_bytes=8192 if role=='source' else 0))
   self.write(role,rows)
  for role in ('source','relay','receiver'):
   (self.root/(role+'.json')).write_text(json.dumps(dict(video=16,pcm_sample_frames=25600,pcm_bytes=102400,terminal=True,held_last_proof=role!='source',same_allocation_checked=role=='relay')))
 def tearDown(self):self.tmp.cleanup()
 def write(self,role,rows):
  with (self.root/(role+'.csv')).open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
 def mutate(self,index,key,value):
  with (self.root/'receiver.csv').open() as f:rows=list(csv.DictReader(f))
  rows[index][key]=value;self.write('receiver',rows)
 def test_valid(self):self.assertTrue(check(self.root)['pass'])
 def test_hash_corruption(self):
  self.mutate(4,'hash',999)
  with self.assertRaisesRegex(ValueError,'oracle mismatch'):check(self.root)
 def test_missing_identity(self):
  self.mutate(4,'identity',5)
  with self.assertRaisesRegex(ValueError,'identities'):check(self.root)
 def test_dts_introduced(self):
  self.mutate(4,'metadata_flags',3)
  with self.assertRaisesRegex(ValueError,'metadata'):check(self.root)
 def test_offset_corruption(self):
  self.mutate(4,'mapped_video_pts_ns',1)
  with self.assertRaisesRegex(ValueError,'A/V'):check(self.root)
 def test_unaccounted_second_readback(self):
  self.mutate(15,'oracle_readback_bytes',8192)
  with self.assertRaisesRegex(ValueError,'copy'):check(self.root)
 def test_held_proof_missing(self):
  p=self.root/'relay.json';r=json.loads(p.read_text());r['held_last_proof']=False;p.write_text(json.dumps(r))
  with self.assertRaisesRegex(ValueError,'held-reader'):check(self.root)
if __name__=='__main__':unittest.main()
