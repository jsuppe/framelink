from pathlib import Path
import subprocess,json,time,re
r=Path('<linux-home>/dev/framelink-m6');b='<linux-home>/dev/build/framelink-m6-linux';out=Path('<linux-home>/dev/evidence/m6-automation/linux-negatives');out.mkdir(exist_ok=True)
s={'state':'running','started':time.time(),'scope':'local reproduction of new CI registration and deterministic regression selection; not hosted workflow result','steps':[]}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
def run(cmd,label):
 with (out/(label+'.log')).open('w') as f:p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,timeout=600)
 s['steps'].append({'name':label,'exit':p.returncode});save();assert p.returncode==0,label
save()
try:
 run(['cmake','--build',b,'-j4'],'build')
 run(['python3',str(r/'tools/check_av_test_registration.py'),b],'registration')
 regex='^(trace_.*|read_ledger|receive_queue|read_leases|read_lease_egl(_producer_death|_delayed_completion)?|gst_(pool_policy|policy_generation|av_timeline)|m4_transport_(jitter_drift|negative_retimestamps)|m5a_transport_relay_sync_negative)$'
 run(['ctest','--test-dir',b,'--output-on-failure','--no-tests=error','-R',regex],'selected-tests')
 text=(out/'selected-tests.log').read_text();s['skips']=re.findall(r'\d+ - ([^\n]+) \(Skipped\)',text)
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
