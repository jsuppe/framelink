#!/usr/bin/env python3
"""Serial M6 deterministic gates; exit0=all pass,1=failed/missing,77=unsupported skips.

Build first. Desktop uses CTest/JUnit. Android pushes selected executables to a
fresh directory beneath --remote-dir; shared runtime libraries must already be
installed there. Output retains logs, command provenance and separate skips.
This is not endurance, performance-budget, presentation or final qualification.
"""
import argparse, hashlib, json, os, pathlib, re, shlex, subprocess, sys, time, uuid
import xml.etree.ElementTree as ET
TRACE = {'trace_'+x for x in ['disabled','default_capacity','override_capacity','invalid_capacity','bounded_concurrent','thread_limit','details','compiled_out']}
COMMON = {'read_ledger','receive_queue','read_leases','gst_pool_policy','gst_policy_generation','gst_av_timeline','m4_transport_jitter_drift','m4_transport_negative_retimestamps','m5a_transport_relay_sync_negative'}
def classify_junit(path, expected):
    rows = ET.parse(path).getroot().findall('.//testcase')
    if len(rows)!=1 or rows[0].get('name')!=expected: return 'missing'
    row=rows[0]
    if row.find('failure') is not None or row.find('error') is not None:return 'fail'
    if row.find('skipped') is not None or row.get('status')=='notrun':return 'skip'
    return 'pass'
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build',required=True);ap.add_argument('--config');ap.add_argument('--out',required=True);ap.add_argument('--platform',choices=['linux','windows','android'],required=True);ap.add_argument('--adb');ap.add_argument('--adb-bin',default='adb');ap.add_argument('--remote-dir',default='/data/local/tmp/framelink-m6');a=ap.parse_args()
    if (a.platform=='android')!=bool(a.adb):ap.error('Android requires --adb; desktop must omit it')
    out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=False)
    cmd=['ctest','--test-dir',a.build,'--show-only=json-v1']+(['-C',a.config] if a.config else [])
    data=json.loads(subprocess.check_output(cmd,text=True));(out/'registration.json').write_text(json.dumps(data,indent=2))
    tests={t['name']:t for t in data['tests']};backend='d3d11' if a.platform=='windows' else 'egl'
    required=TRACE|COMMON|{'read_lease_'+backend+s for s in ['', '_producer_death','_delayed_completion']}
    report={'runner_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'build_directory':str(pathlib.Path(a.build).resolve()),'configuration':a.config,'scope':__doc__,'platform':a.platform,'expected':sorted(required),'missing':sorted(required-tests.keys()),'tests':[],'result':'running'}
    def save():(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    save()
    if report['missing']:report['result']='fail';save();return 1
    adb=[a.adb_bin,'-s',a.adb] if a.adb else None;remote=a.remote_dir+'/m6-gates-'+uuid.uuid4().hex[:12]
    if adb:subprocess.run(adb+['shell','mkdir -p '+shlex.quote(remote)],check=True)
    pushed=set()
    for name in sorted(required):
        test=tests[name];props={x['name']:x['value'] for x in test.get('properties',[])};row={'name':name};start=time.monotonic()
        try:
            executable=pathlib.Path(test['command'][0]);row['binary_sha256']=hashlib.sha256(executable.read_bytes()).hexdigest()
            if adb:
                exe=pathlib.Path(test['command'][0]);row['binary_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
                if str(exe) not in pushed:subprocess.run(adb+['push',str(exe),remote+'/'+exe.name],check=True,capture_output=True);pushed.add(str(exe))
                scratch=remote+'/'+name;env={'LD_LIBRARY_PATH':a.remote_dir,'TMPDIR':scratch,'FRAMELINK_TRACE':''}
                for item in props.get('ENVIRONMENT',[]):
                    k,v=item.split('=',1);env[k]=scratch+'/'+pathlib.PurePath(v).name if k.endswith('_TRACE') and v else v
                args=['static' if pathlib.PurePath(x).name=='libgstframelink.a' else x for x in test['command'][1:]]
                shell='mkdir -p '+shlex.quote(scratch)+' && cd '+shlex.quote(scratch)+' && env '+shlex.join([k+'='+v for k,v in env.items()]+[remote+'/'+exe.name]+args)
                command=adb+['shell',shell]
            else:
                junit=out/(name+'.xml');command=['ctest','--test-dir',a.build,'--no-tests=error','--output-on-failure','-R','^'+re.escape(name)+'$','--output-junit',str(junit.resolve())]+(['-C',a.config] if a.config else [])
            row['command']=command;p=subprocess.run(command,capture_output=True,text=True,timeout=float(props.get('TIMEOUT',120))+30);(out/(name+'.log')).write_text(p.stdout+p.stderr);row['exit']=p.returncode
            row['result']=('skip' if p.returncode==props.get('SKIP_RETURN_CODE',77) else 'pass' if p.returncode==0 else 'fail') if adb else classify_junit(junit,name)
            if not adb and p.returncode and row['result']=='pass':row['result']='fail'
        except Exception as e:row.update(result='fail',error=str(e))
        row['seconds']=time.monotonic()-start;report['tests'].append(row);save()
    states={t['result'] for t in report['tests']};report['result']='fail' if states-{'pass','skip'} else 'incomplete' if 'skip' in states else 'pass';save()
    print(json.dumps({'result':report['result'],'counts':{s:sum(t['result']==s for t in report['tests']) for s in states}}))
    return {'pass':0,'fail':1,'incomplete':77}[report['result']]
if __name__=='__main__':sys.exit(main())
