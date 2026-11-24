#!/usr/bin/env python3
import copy,csv,json,pathlib,tempfile,unittest
from check_local_native_bench import analyze,resource_analysis
class Checks(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=pathlib.Path(self.tmp.name)
  self.config=dict(seconds=1,fps=30,width=64,height=32,format='BGRA',trace=False,clock='QPC quotient-remainder nanoseconds',pool=2,queue=1,offset_ns=37000000)
  (self.root/'config.json').write_text(json.dumps(self.config))
  for role in ('source','relay','receiver'):
   rows=[]
   for n in range(30):
    due=1000000000+n*10**9//30
    rows.append(dict(identity=n,pts_ns=37000000+n*10**9//30,scheduled_ns=0 if role=='relay' else due,source_ns=0 if role=='relay' else due+100,arrival_ns=due+{'source':200,'relay':300,'receiver':500}[role],completed_ns=0 if role=='source' else due+{'relay':400,'receiver':600}[role],copy_bytes=0 if role=='relay' else 8192,sequence=n,slot=n%2,generation=1,metadata_version=1,metadata_flags=3,dts_ns=37000000+n*10**9//30,duration_ns=(n+1)*10**9//30-n*10**9//30,lease=n+1 if role=='receiver' else 0,completion_value=n+1 if role=='receiver' else 0,mapped_pts_ns=37000000+n*10**9//30))
   for r in rows:
    due=1000000000+r['identity']*10**9//30
    for key,delta in [('acquire_begin_ns',10),('acquired_ns',30),('upload_begin_ns',110),('uploaded_ns',130),('submit_begin_ns',150),('submitted_ns',180)]:r[key]=due+delta if role=='source' else 0
    r['forward_begin_ns']=due+320 if role=='relay' else 0
    r['forwarded_ns']=due+380 if role=='relay' else 0
   self.write(role+'.video.csv',rows)
   self.write(role+'.audio.csv',[dict(identity=n,pts_ns=n*10000000,scheduled_ns=1000000000+n*10000000,source_ns=1000000100+n*10000000,arrival_ns=1000000000+n*10000000+{'source':200,'relay':300,'receiver':400}[role],samples=480,bytes=1920,observed_pts_ns=n*10000000,mapped_pts_ns=n*10000000) for n in range(100)])
   self.write(role+'.resources.csv',[dict(t_ns=1000000000+n*1000000000,cpu_ns=n*1000000,memory_kb=1000,handles=20,threads=4) for n in range(3)])
   (self.root/(role+'.summary.json')).write_text(json.dumps(dict(video=30,audio_blocks=100,pcm_frames=48000,terminal=True,held_last_proof=role!='source')))
 def tearDown(self):self.tmp.cleanup()
 def write(self,name,rows):
  with (self.root/name).open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
 def mutate(self,name,index,key,value):
  with (self.root/name).open() as f:rows=list(csv.DictReader(f))
  rows[index][key]=value;self.write(name,rows)
 def test_valid(self):self.assertTrue(analyze(self.root)['pass_all'])
 def test_profile_must_match_request(self):
  (self.root/'config.json').write_text(json.dumps(dict(self.config,pool=4,queue=2)))
  with self.assertRaisesRegex(ValueError,'profile changed'):analyze(self.root)
  self.assertTrue(analyze(self.root,4,2)['pass_all'])
  with self.assertRaisesRegex(ValueError,'profile changed'):analyze(self.root,4,1)
 def test_dropped_identity(self):
  self.mutate('receiver.video.csv',3,'identity',4)
  with self.assertRaises(ValueError):analyze(self.root)
 def test_changed_pcm(self):
  self.mutate('relay.audio.csv',7,'samples',479)
  with self.assertRaises(ValueError):analyze(self.root)
 def test_early_completion(self):
  self.mutate('receiver.video.csv',3,'completed_ns',0)
  with self.assertRaises(ValueError):analyze(self.root)
 def test_late_startup(self):
  self.mutate('receiver.video.csv',0,'arrival_ns',1040000000);self.mutate('receiver.video.csv',0,'completed_ns',1040000010)
  with self.assertRaisesRegex(ValueError,'arrival diagnostic'):analyze(self.root)
 def test_wrong_mapping(self):
  self.mutate('receiver.audio.csv',7,'pts_ns',70000001)
  with self.assertRaises(ValueError):analyze(self.root)
 def test_nan_resource(self):
  self.mutate('receiver.resources.csv',1,'memory_kb','nan')
  with self.assertRaises(ValueError):analyze(self.root)
 def test_missing_trace(self):
  self.config['trace']=True;(self.root/'config.json').write_text(json.dumps(self.config))
  with self.assertRaisesRegex(ValueError,'trace'):analyze(self.root)
 def test_missing_completion(self):
  self.mutate('receiver.video.csv',3,'completion_value',0)
  with self.assertRaisesRegex(ValueError,'completion'):analyze(self.root)
 def test_generation_change(self):
  self.mutate('relay.video.csv',3,'generation',2)
  with self.assertRaisesRegex(ValueError,'generation'):analyze(self.root)
 def test_hidden_copy(self):
  self.mutate('relay.video.csv',3,'copy_bytes',8192)
  with self.assertRaisesRegex(ValueError,'copy'):analyze(self.root)
 def test_trace_loss(self):
  self.config['trace']=True;(self.root/'config.json').write_text(json.dumps(self.config))
  (self.root/'source.trace.csv.1.csv').write_text('# dropped_events=1,rejected_threads=0,capacity_per_thread=1\n')
  with self.assertRaisesRegex(ValueError,'trace loss'):analyze(self.root)
 def traces(self):
  self.config['trace']=True;(self.root/'config.json').write_text(json.dumps(self.config))
  for role,ops in [('source',[('flEndSubmit','up')]),('relay',[('flAcquireFrame','up'),('flEndSubmit','down')]),('receiver',[('flAcquireFrame','down')])]:
   rows=[dict(operation=op,channel_id=channel,fence=n+1,pts_ns=37000000+n*10**9//30,result=0,payload_bytes=0) for op,channel in ops for n in range(30)]
   name=role+'.trace.csv.1.csv';self.write(name,rows)
   with (self.root/name).open('a') as f:f.write('# dropped_events=0,rejected_threads=0,capacity_per_thread=1000\n')
 def test_valid_trace(self):
  self.traces();self.assertTrue(analyze(self.root)['trace_checked'])
 def test_trace_wrong_identity(self):
  self.traces();p=self.root/'receiver.trace.csv.1.csv';s=p.read_text().replace('down,1,','down,99,',1);p.write_text(s)
  with self.assertRaisesRegex(ValueError,'identities'):analyze(self.root)
 def test_trace_copy(self):
  self.traces();p=self.root/'relay.trace.csv.1.csv';s=p.read_text().replace(',0,0\n',',0,100\n',1);p.write_text(s)
  with self.assertRaisesRegex(ValueError,'payload copy'):analyze(self.root)
 def test_trace_truncated(self):
  self.traces();p=self.root/'source.trace.csv.1.csv';rows=p.read_text().splitlines();del rows[3];p.write_text('\n'.join(rows)+'\n')
  with self.assertRaisesRegex(ValueError,'role counts'):analyze(self.root)
 def test_observed_pcm_mapping(self):
  self.mutate('receiver.audio.csv',3,'observed_pts_ns',30000001)
  with self.assertRaisesRegex(ValueError,'actual PCM'):analyze(self.root)
 def test_final_before_relay_return(self):
  self.mutate('relay.video.csv',0,'completed_ns',1000000900)
  self.assertTrue(analyze(self.root)['pass_all'])
 def resources(self,seconds=600,growth=10):
  return [dict(t_ns=n*10**9,cpu_ns=n*10**7,memory_kb=1000+n*growth/60,handles=20,threads=4) for n in range(seconds+1)]
 def test_resource_sustained(self):
  for seconds in [600,1800]:
   slopes,cpu=resource_analysis(self.resources(seconds),seconds,'test');self.assertAlmostEqual(slopes['memory_kb'],10);self.assertAlmostEqual(cpu,1)
 def test_resource_growth(self):
  with self.assertRaisesRegex(ValueError,'slope gate'):resource_analysis(self.resources(growth=65),600,'test')
 def test_resource_truncated(self):
  with self.assertRaisesRegex(ValueError,'truncated'):resource_analysis(self.resources()[:-10],600,'test')
 def test_resource_negative_counter(self):
  for key in ['t_ns','cpu_ns','memory_kb','handles','threads']:
   r=self.resources();r[1][key]=-1
   with self.assertRaises(ValueError):resource_analysis(r,600,'test')
 def test_resource_decreasing_cpu(self):
  r=self.resources();r[2]['cpu_ns']=r[1]['cpu_ns']-1
  with self.assertRaisesRegex(ValueError,'decreasing CPU'):resource_analysis(r,600,'test')
 def test_resource_degenerate(self):
  r=self.resources();r[1]['t_ns']=0
  with self.assertRaisesRegex(ValueError,'nonmonotonic'):resource_analysis(r,600,'test')
 def test_source_phase_order(self):
  self.mutate('source.video.csv',0,'uploaded_ns',1000000109)
  with self.assertRaisesRegex(ValueError,'source phase order'):analyze(self.root)
 def test_relay_phase_order(self):
  self.mutate('relay.video.csv',0,'forwarded_ns',1000000319)
  with self.assertRaisesRegex(ValueError,'forward phase order'):analyze(self.root)
 def test_phase_distributions(self):
  timing=analyze(self.root)['timing_ms']
  self.assertAlmostEqual(timing['source_acquire_call']['max'],20/1e6)
  self.assertAlmostEqual(timing['source_upload_call']['max'],20/1e6)
  self.assertAlmostEqual(timing['source_submit_call']['max'],30/1e6)
  self.assertAlmostEqual(timing['relay_forward_call']['max'],60/1e6)
  self.assertAlmostEqual(timing['relay_acquire_to_record']['max'],100/1e6)
 def test_metadata_version(self):
  self.mutate('receiver.video.csv',0,'metadata_version',2)
  with self.assertRaisesRegex(ValueError,'metadata'):analyze(self.root)
 def test_metadata_flags(self):
  self.mutate('relay.video.csv',0,'metadata_flags',7)
  with self.assertRaisesRegex(ValueError,'metadata'):analyze(self.root)
 def test_unfinished(self):
  p=self.root/'relay.summary.json';s=json.loads(p.read_text());s['terminal']=False;p.write_text(json.dumps(s))
  with self.assertRaisesRegex(ValueError,'terminal'):analyze(self.root)
if __name__=='__main__':unittest.main()
