"""Analyze captured gst-launch output locally. No rate acceptance or decode claim."""
import hashlib,json,pathlib,re,statistics,sys
p=pathlib.Path(sys.argv[1]);s=json.loads((p/'status.json').read_text(encoding='utf-8-sig'));result={'scope':s['scope'],'arms':{}}
manifest=json.loads((p/'sha256.json').read_text())
for n,h in manifest.items():
    assert hashlib.sha256((p/n).read_bytes()).hexdigest()==h,n
for arm in ['gen-clean','encode-clean','gen-count','encode-count']:
    if arm.endswith('-clean'):
        result['arms'][arm]={'wall_seconds':s[arm]['wall_ns']/1e9,'exit_code':s[arm]['exit_code'],'requested_frames':150,'independent_count_and_pts':'not observed in clean wall-time arm'}
        continue
    log=(p/(arm+'.log')).read_text(errors='replace')
    lines=[x for x in log.splitlines() if 'GstIdentity:probe:' in x and 'last-message = chain' in x]
    pts=[]
    for x in lines:
        m=re.search(r'pts: (\d+):(\d+):(\d+)\.(\d{9})',x)
        assert m,x
        h,mn,sec,ns=map(int,m.groups());pts.append(((h*60+mn)*60+sec)*10**9+ns)
    assert len(pts)==150,(arm,len(pts))
    assert [v-pts[0] for v in pts]==[i*10**9//30 for i in range(150)],arm+' PTS'
    a={'count':len(pts),'pts_origin_ns':pts[0],'pts_exact':True,'wall_seconds':s[arm]['wall_ns']/1e9,'exit_code':s[arm]['exit_code']}
    # stats tracer schemas are discovered from the captured new-element records.
    trace=(p/(arm+'.trace.log')).read_text(errors='replace')
    ix=set()
    for line in trace.splitlines():
        if 'new-element,' in line and re.search(r'name=\(string\)"?probe(?:"|[,;])',line):
            m=re.search(r'(?:^|[ ,])ix=\(uint\)(\d+)',line)
            if m:ix.add(int(m.group(1)))
    stamps=[]
    for line in trace.splitlines():
        if ' buffer, ' not in line:continue
        e=re.search(r'element-ix=\(uint\)(\d+)',line);t=re.search(r'(?:[ ,])ts=\(guint64\)(\d+)',line)
        if e and t and int(e.group(1)) in ix:stamps.append(int(t.group(1)))
    if len(ix)==1 and len(stamps)==150 and all(b>=a for a,b in zip(stamps,stamps[1:])):
        gaps=[(b-a)/1e6 for a,b in zip(stamps,stamps[1:])]
        a.update(probe_timing='validated stats tracer buffer timestamps',first_to_last_ms=(stamps[-1]-stamps[0])/1e6,interval_median_ms=statistics.median(gaps),interval_max_ms=max(gaps),interval_mean_ms=statistics.mean(gaps))
    else:a.update(probe_timing='unavailable: stats tracer schema/count not validated',probe_elements=sorted(ix),probe_timestamp_count=len(stamps))
    result['arms'][arm]=a
result['caution']='Instrumented arms include tracer/verbose overhead; clean arms do not independently observe counts. Wall time includes process/plugin startup and drain. Fast arms do not exonerate combined-load contention; slow arms establish a ceiling only for this instrumented upstream workload.'
print(json.dumps(result,indent=2))
