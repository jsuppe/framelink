#!/usr/bin/env python3
import csv,hashlib,json,pathlib,tempfile,unittest
from check_local_decoder_rate import analyze,resource_analysis
FIELDS='identity pts_ns mapped_pts_ns generation metadata_version metadata_flags dts_ns duration_ns decoded_pts_ns decoded_dts_ns decoded_origin_ns scheduled_ns acquire_begin_ns acquired_ns decode_begin_ns decoded_ns convert_begin_ns converted_ns conversion_done_ns oracle_begin_ns oracle_done_ns submit_begin_ns submitted_ns arrival_ns forward_begin_ns forwarded_ns copy_begin_ns signalled_ns gpu_done_ns lease_done_ns hash_begin_ns hash_done_ns readback_bytes converted_bytes slot sequence ready_value lease completion_value native_matches'.split()
class DecoderRateChecks(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=pathlib.Path(self.tmp.name)
  self.config=dict(schema=1,seconds=1,fps=30,width=64,height=32,decoder_width=320,decoder_height=240,expected_video=30,expected_audio_blocks=100,expected_pcm_frames=48000,format='RGBA',trace=False,clock='QPC quotient-remainder nanoseconds',digest='SHA256 active rows',pool=2,queue=1,offset_ns=37000000)
  (self.root/'config.json').write_text(json.dumps(self.config))
  for role in ('source','relay','receiver'):
   rows=[]
   for n in range(30):
    due=10**9+n*10**9//30;pts=37000000+n*10**9//30;r=dict.fromkeys(FIELDS,0)
    r.update(identity=n,pts_ns=pts,mapped_pts_ns=pts+(50000000 if role=='receiver' else 0),generation=7,metadata_version=1,metadata_flags=2,duration_ns=10**9//30,slot=(1-n%2 if role=='receiver' else n%2),digest=hashlib.sha256(str(n).encode()).hexdigest() if role!='relay' else '-')
    if role=='source':
     r.update(decoded_pts_ns=3600*10**9+n*10**9//30,decoded_dts_ns=2**64-1,decoded_origin_ns=3600*10**9,scheduled_ns=due,readback_bytes=8192,converted_bytes=8192)
     for key,delta in [('acquire_begin_ns',10),('acquired_ns',20),('decode_begin_ns',30),('decoded_ns',40),('convert_begin_ns',50),('converted_ns',60),('conversion_done_ns',70),('oracle_begin_ns',80),('oracle_done_ns',90),('submit_begin_ns',100),('submitted_ns',1100)]:r[key]=due+delta
    else:r.update(sequence=n+1,ready_value=n+1,arrival_ns=due+(200 if role=='relay' else 400))
    if role=='relay':r.update(forward_begin_ns=due+300,forwarded_ns=due+1200,native_matches=1)
    if role=='receiver':r.update(copy_begin_ns=due+500,signalled_ns=due+600,gpu_done_ns=due+700,lease_done_ns=due+800,hash_begin_ns=due+900,hash_done_ns=due+1000,readback_bytes=8192,lease=n+1,completion_value=n+1)
    rows.append(r)
   self.write(role+'.video.csv',rows)
   offset=50000000 if role=='receiver' else 0
   self.write(role+'.audio.csv',[dict(identity=n,pts_ns=n*10000000,scheduled_ns=10**9+n*10000000,source_ns=10**9+n*10000000+100,arrival_ns=10**9+n*10000000+{'source':200,'relay':300,'receiver':400}[role],samples=480,bytes=1920,observed_pts_ns=n*10000000+offset,mapped_pts_ns=n*10000000+offset) for n in range(100)])
   self.write(role+'.resources.csv',[dict(t_ns=10**9+n*10**9,cpu_ns=n*10000000,memory_kb=1000,handles=20,threads=4) for n in range(3)])
   (self.root/(role+'.summary.json')).write_text(json.dumps(dict(video=30,audio_blocks=100,pcm_frames=48000,terminal=True,held_last_proof=role!='source',same_allocation_checked=role=='relay')))
 def tearDown(self):self.tmp.cleanup()
 def write(self,name,rows):
  with (self.root/name).open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=rows[0]);w.writeheader();w.writerows(rows)
 def mutate(self,name,index,key,value):
  with (self.root/name).open() as f:rows=list(csv.DictReader(f))
  rows[index][key]=value;self.write(name,rows)
 def test_valid_including_receiver_before_submit_returns(self):
  result=analyze(self.root);self.assertTrue(result['pass']);self.assertFalse(result['trace_qualified']);self.assertEqual(result['source_conversion_bytes'],30*8192)
 def test_changed_digest(self):
  self.mutate('receiver.video.csv',3,'digest','f'*64)
  with self.assertRaisesRegex(ValueError,'digest mismatch'):analyze(self.root)
 def test_repeated_nonconstant_source_and_final_pixels(self):
  for role in ('source','receiver'):
   with (self.root/(role+'.video.csv')).open() as f:rows=list(csv.DictReader(f))
   for n,row in enumerate(rows):row['digest']=hashlib.sha256(str(n%2).encode()).hexdigest()
   self.write(role+'.video.csv',rows)
  with self.assertRaisesRegex(ValueError,'distinct for every expected frame'):analyze(self.root)
 def test_missing_identity(self):
  self.mutate('receiver.video.csv',3,'identity',4)
  with self.assertRaisesRegex(ValueError,'identities'):analyze(self.root)
 def test_config_truncation(self):
  self.config['expected_video']=29;(self.root/'config.json').write_text(json.dumps(self.config))
  with self.assertRaisesRegex(ValueError,'expected counts'):analyze(self.root)
 def test_wrong_decoder_origin(self):
  self.mutate('source.video.csv',3,'decoded_origin_ns',0)
  with self.assertRaisesRegex(ValueError,'decoder PTS'):analyze(self.root)
 def test_decoded_dts_invented(self):
  self.mutate('source.video.csv',3,'decoded_dts_ns',0)
  with self.assertRaisesRegex(ValueError,'decoder PTS/DTS'):analyze(self.root)
 def test_metadata_changed(self):
  self.mutate('receiver.video.csv',3,'metadata_flags',3)
  with self.assertRaisesRegex(ValueError,'metadata'):analyze(self.root)
 def test_source_phase_regression(self):
  self.mutate('source.video.csv',3,'conversion_done_ns',1)
  with self.assertRaisesRegex(ValueError,'phase order'):analyze(self.root)
 def test_fake_completion(self):
  self.mutate('receiver.video.csv',3,'gpu_done_ns',1)
  with self.assertRaisesRegex(ValueError,'completion phase'):analyze(self.root)
 def test_missing_lease(self):
  self.mutate('receiver.video.csv',3,'lease',0)
  with self.assertRaisesRegex(ValueError,'lease/fence'):analyze(self.root)
 def test_identity_not_checked(self):
  self.mutate('relay.video.csv',3,'native_matches',0)
  with self.assertRaisesRegex(ValueError,'identity match'):analyze(self.root)
 def test_relay_copy(self):
  self.mutate('relay.video.csv',3,'readback_bytes',8192)
  with self.assertRaisesRegex(ValueError,'copy'):analyze(self.root)
 def test_pcm_corruption(self):
  self.mutate('receiver.audio.csv',3,'samples',479)
  with self.assertRaisesRegex(ValueError,'PCM cadence'):analyze(self.root)
 def test_av_offset(self):
  self.mutate('receiver.video.csv',3,'mapped_pts_ns',1)
  with self.assertRaisesRegex(ValueError,'common A/V'):analyze(self.root)
 def test_slot_rebind(self):
  self.mutate('receiver.video.csv',3,'slot',1)
  with self.assertRaisesRegex(ValueError,'allocation slot'):analyze(self.root)
 def test_native_replay(self):
  self.mutate('receiver.video.csv',3,'sequence',3)
  with self.assertRaisesRegex(ValueError,'sequence/fence'):analyze(self.root)
 def test_arrival_miss_reported(self):
  for key in ['arrival_ns','copy_begin_ns','signalled_ns','gpu_done_ns','lease_done_ns','hash_begin_ns','hash_done_ns']:self.mutate('receiver.video.csv',0,key,10**9+35000000)
  result=analyze(self.root);self.assertFalse(result['pass']);self.assertEqual(result['video_arrival_misses'],[0])
 def test_reject_trace_claim(self):
  self.config['trace']=True;(self.root/'config.json').write_text(json.dumps(self.config))
  with self.assertRaisesRegex(ValueError,'trace'):analyze(self.root)
 def test_profile_input_too_small(self):
  self.config.update(width=1920,height=1080,fps=60,expected_video=60)
  (self.root/'config.json').write_text(json.dumps(self.config))
  with self.assertRaisesRegex(ValueError,'input not target'):analyze(self.root)
 def traces(self):
  self.config.update(trace=True,trace_capacity_per_thread=4484)
  (self.root/'config.json').write_text(json.dumps(self.config))
  for role,ops in [('source',[('flEndSubmit','up','relay')]),('relay',[('flAcquireFrame','up','relay'),('flEndSubmit','down','receiver')]),('receiver',[('flAcquireFrame','down','receiver')])]:
   rows=[]
   for op,channel,observed in ops:
    with (self.root/(observed+'.video.csv')).open() as f:video=list(csv.DictReader(f))
    rows.extend(dict(operation=op,channel_id=channel,fence=v['ready_value'],slot=v['slot'],sequence=v['sequence'] if op=='flAcquireFrame' else 0,pts_ns=v['pts_ns'],result=0,payload_bytes=0) for v in video)
   name=role+'.trace.csv.1.csv';self.write(name,rows)
   path=self.root/name
   path.write_text('# schema=2,clock=steady_ns,pid=1,origin_ns=0,wall_ns=0\n'+path.read_text()+'# dropped_events=0,rejected_threads=0,capacity_per_thread=4484\n')
 def trace_change(self,role,old,new):
  path=self.root/(role+'.trace.csv.1.csv');path.write_text(path.read_text().replace(old,new,1))
 def test_valid_trace(self):
  self.traces();result=analyze(self.root)
  self.assertTrue(result['trace_checked']);self.assertFalse(result['trace_qualified']);self.assertTrue(result['pass'])
  self.assertEqual(result['trace_counts']['relay'],dict(sent=30,received=30))
 def test_trace_loss_and_rejected_threads(self):
  for old,new in [('dropped_events=0','dropped_events=1'),('rejected_threads=0','rejected_threads=1'),('capacity_per_thread=4484','capacity_per_thread=4485')]:
   self.traces();self.trace_change('relay',old,new)
   with self.assertRaisesRegex(ValueError,'trace loss/capacity'):analyze(self.root)
 def test_trace_native_identity(self):
  self.traces();self.trace_change('receiver',',1,1,1,37000000',',1,1,9,37000000')
  with self.assertRaisesRegex(ValueError,'CSV native identity'):analyze(self.root)
 def test_trace_boundary_identity(self):
  self.traces();self.trace_change('source','flEndSubmit,up,1,','flEndSubmit,up,99,')
  with self.assertRaisesRegex(ValueError,'boundary identities'):analyze(self.root)
 def test_trace_relay_copy(self):
  self.traces();self.trace_change('relay',',37000000,0,0',',37000000,0,8192')
  with self.assertRaisesRegex(ValueError,'payload copy'):analyze(self.root)
 def test_trace_relay_map(self):
  self.traces();self.trace_change('relay','flAcquireFrame','flImageMapPlanes')
  with self.assertRaisesRegex(ValueError,'payload map'):analyze(self.root)
 def test_trace_truncated(self):
  self.traces();path=self.root/'source.trace.csv.1.csv';lines=path.read_text().splitlines();del lines[3];path.write_text('\n'.join(lines)+'\n')
  with self.assertRaisesRegex(ValueError,'role counts'):analyze(self.root)
 def test_trace_duration_limit(self):
  from check_local_decoder_rate import trace_analysis
  with self.assertRaisesRegex(ValueError,'duration/capacity'):trace_analysis(self.root,dict(seconds=6,expected_video=180),{})
 def resources(self,seconds=600,growth=10):return [dict(t_ns=n*10**9,cpu_ns=n*10000000,memory_kb=1000+n*growth/60,handles=20,threads=4) for n in range(seconds+1)]
 def test_resource_positive(self):
  for seconds in [600,1800]:self.assertAlmostEqual(resource_analysis(self.resources(seconds),seconds)['slopes_per_min']['memory_kb'],10)
 def test_resource_failures(self):
  for rows in [self.resources(growth=65),self.resources()[:-10]]:
   with self.assertRaises(ValueError):resource_analysis(rows,600)
  for key,value in [('memory_kb',float('nan')),('handles',-1),('t_ns',0),('cpu_ns',0)]:
   rows=self.resources();rows[2][key]=value
   with self.assertRaises(ValueError):resource_analysis(rows,600)
if __name__=='__main__':unittest.main()
