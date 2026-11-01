#!/usr/bin/env python3
"""Exercise dedicated virtual PulseAudio output, missing device and caps rejection.
Does not change the default device or establish physical speaker qualification.
"""
import argparse
import json
import os
import pathlib
import subprocess


def command(args, timeout=15):
    return subprocess.run(args, capture_output=True, text=True, timeout=timeout)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=pathlib.Path, required=True)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    name='framelink_m5_'+str(os.getpid())
    loaded=command(['pactl','load-module','module-null-sink','sink_name='+name])
    assert loaded.returncode == 0, loaded.stderr
    module=loaded.stdout.strip()
    results=[]
    try:
        cases=[('dedicated-null', ['audiotestsrc','num-buffers=50','!','audioconvert','!','pulsesink','device='+name], True),
               ('missing-device-server-fallback', ['audiotestsrc','num-buffers=10','!','pulsesink','device='+name+'_missing'], None),
               ('missing-server', ['audiotestsrc','num-buffers=10','!','pulsesink','server=unix:/tmp/framelink-m5-no-such-server-'+str(os.getpid())], False),
               ('unsupported-audio-format', ['audiotestsrc','num-buffers=10','!','audio/x-raw,format=INVALID','!','pulsesink','device='+name],False)]
        for title, pipeline, success in cases:
            r=command(['gst-launch-1.0','-q']+pipeline)
            (args.output/(title+'.log')).write_text(r.stdout+r.stderr)
            if success is not None:
                assert (r.returncode==0)==success, (title,r.returncode,r.stderr)
            results.append({'case':title,'exit_code':r.returncode,'expected_success':success})
    finally:
        unloaded=command(['pactl','unload-module',module])
        assert unloaded.returncode==0,unloaded.stderr
    remaining=command(['pactl','list','short','sinks'])
    assert name not in remaining.stdout
    results.append({'case':'dedicated-module-cleanup','passed':True})
    (args.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results))


if __name__=='__main__': main()
