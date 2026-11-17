from pathlib import Path
import subprocess,json,time,base64
r=Path('<linux-home>/dev/framelink-m6');out=Path('<linux-home>/dev/evidence/m6-automation/windows-negatives');out.mkdir(exist_ok=True);remote='<win-home>/dev/framelink-m6-20261106'
s={'state':'running','started':time.time(),'scope':'local Windows reproduction of selected CI negatives, not hosted Actions execution','steps':[]}
def save():(out/'status.json').write_text(json.dumps(s,indent=2))
def ps(body,label):
 code="$env:PATH='C:\\Program Files\\gstreamer\\1.0\\msvc_x86_64\\bin;'+$env:PATH; Set-Location '"+remote+"'; "+body
 with (out/(label+'.log')).open('w') as f:p=subprocess.run(['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()],stdout=f,stderr=subprocess.STDOUT,timeout=600)
 s['steps'].append({'name':label,'exit':p.returncode});save();assert p.returncode==0,label
save()
try:
 # Use an isolated checker filename; do not overwrite remote tooling.
 subprocess.run(['scp','-q',str(r/'tools/check_av_test_registration.py'),'<win-user>@<win-host>:'+remote+'/m6_check_registration.py'],check=True)
 ps('cmake --build build --config Release -j 4; exit $LASTEXITCODE','build')
 ps('python m6_check_registration.py build Release; exit $LASTEXITCODE','registration')
 regex='^(trace_.*|read_ledger|receive_queue|read_leases|read_lease_d3d11(_producer_death|_delayed_completion)?|gst_(pool_policy|policy_generation|av_timeline)|m4_transport_(jitter_drift|negative_retimestamps)|m5a_transport_relay_sync_negative)$'
 ps('ctest --test-dir build -C Release --output-on-failure --no-tests=error -R "'+regex+'"; exit $LASTEXITCODE','selected-tests')
 s['state']='finished'
except Exception as e:s.update(state='failed',error=str(e))
s['ended']=time.time();save();print(json.dumps(s));raise SystemExit(s['state']!='finished')
