#!/usr/bin/env python3
"""Run/check the tiny authentic-discovery decoder fixture; not a rate gate."""
import argparse,csv,json,pathlib,subprocess,tempfile

def require(ok,message):
    if not ok:raise ValueError(message)
def read(path):
    with path.open() as f:return [{k:int(v) for k,v in row.items()} for row in csv.DictReader(f)]
def check(root):
    root=pathlib.Path(root);rows={role:read(root/(role+'.csv')) for role in ('source','receiver')}
    for role,data in rows.items():
        require([r['identity'] for r in data]==list(range(16)),role+' missing/duplicate identities')
        origin=data[0]['decoded_pts_ns'];generation=data[0]['generation']
        require(generation>0,role+' invalid generation')
        require(len({r['hash'] for r in data})==16,role+' decoded frames not distinct')
        for n,r in enumerate(data):
            require(r['relative_pts_ns']==n*10**9//30 and r['decoded_pts_ns']-origin==r['relative_pts_ns'],role+' decoder timeline')
            require(r['duration_ns']==10**9//30 and r['decoded_dts_ns']==2**64-1,role+' decoded duration/DTS')
            require(r['video_pts_ns']==37000000+r['relative_pts_ns'],role+' video offset')
            require(r['mapped_video_pts_ns']-r['pcm_pts_ns']==37000000,role+' mapped A/V offset')
            require(r['generation']==generation and r['metadata_version']==1 and r['metadata_flags']==2 and r['dts_ns']==0,role+' metadata')
            require(r['oracle_readback_bytes']==(16384 if role=='receiver' and n==15 else 8192),role+' oracle copy accounting')
            require(r['converted_output_bytes']==(8192 if role=='source' else 0),role+' conversion accounting')
        offset=data[0]['pcm_pts_ns']-data[0]['relative_pts_ns']
        require(all(r['pcm_pts_ns']-r['relative_pts_ns']==offset for r in data),role+' changing PCM mapping')
    for a,b in zip(rows['source'],rows['receiver']):
        require(all(a[k]==b[k] for k in ('identity','decoded_pts_ns','relative_pts_ns','duration_ns','hash','video_pts_ns','generation','metadata_version','metadata_flags','dts_ns','decoded_dts_ns')),'source/final oracle mismatch')
    for role in ('source','relay','receiver'):
        s=json.loads((root/(role+'.json')).read_text())
        require(s['video']==16 and s['pcm_sample_frames']==25600 and s['pcm_bytes']==102400 and s['terminal'] is True,role+' terminal accounting')
        if role!='source':require(s['held_last_proof'] is True,role+' missing held-reader proof')
        if role=='relay':require(s['same_allocation_checked'] is True,'missing same-allocation check')
    return {'pass':True,'video_frames':16,'distinct_decoded_frames':16,'pcm_sample_frames':25600,'mapped_av_offset_ns':37000000,'source_conversion_output_bytes':16*8192,'source_oracle_readback_bytes':16*8192,'final_oracle_readback_bytes':17*8192,'scope':'tiny real decoder conversion plus authenticated same-allocation relay; no target-rate/physical-playback/zero-copy claim'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check-only');p.add_argument('--exe');p.add_argument('--plugin');a=p.parse_args()
    if a.check_only:root=pathlib.Path(a.check_only)
    else:
        if not a.exe or not a.plugin:p.error('--exe and --plugin required unless --check-only')
        base=pathlib.Path(tempfile.mkdtemp(prefix='framelink-local-decoder-'));root=base/'capture';print('Evidence:',base,flush=True)
        with (base/'run.log').open('w') as f:r=subprocess.run([a.exe,a.plugin,'--out',str(root)],stdout=f,stderr=subprocess.STDOUT,timeout=75)
        if r.returncode:return r.returncode
    try:result=check(root)
    except (ValueError,KeyError,OSError) as e:result={'pass':False,'error':str(e)}
    (root/'independent-analysis.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));return 0 if result['pass'] else 1
if __name__=='__main__':raise SystemExit(main())
