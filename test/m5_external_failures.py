#!/usr/bin/env python3
"""External producer deployment failures: bounded exit and rendezvous cleanup.
Leaves logs, not live child processes. No physical devices required.
"""
import argparse,json,pathlib,subprocess,time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    b=a.build.resolve(); plugin=str(b/'libgstframelink.so')
    cases={
      'missing-endpoint':None,
      'missing-plugin':'no_such_framelink_element ! @VIDEO_SINK@ audiotestsrc num-buffers=10 ! @AUDIO_SINK@',
      'unsupported-caps':'videotestsrc num-buffers=10 ! video/x-raw,format=INVALID ! @VIDEO_SINK@ audiotestsrc num-buffers=10 ! @AUDIO_SINK@',
      'missing-media':'filesrc location=/tmp/framelink-m5-no-such-media ! avidemux name=d d.video_0 ! jpegdec ! @VIDEO_SINK@ d.audio_0 ! @AUDIO_SINK@'}
    results=[]
    for name,pipeline in cases.items():
        root=a.output/name;root.mkdir();session=root/'session'
        common=[plugin,'--session',str(session),'--format','BGRA','--frames','10','--timeout-ms','1000']
        children=[];handles=[];started=time.monotonic()
        try:
            if pipeline is not None:
                path=root/'source.pipeline';path.write_text(pipeline+'\n')
                f=(root/'producer.log').open('w');handles.append(f)
                children.append(subprocess.Popen([str(b/'framelink_av_producer'),*common,'--input-pipeline',str(path)],stdout=f,stderr=subprocess.STDOUT))
            f=(root/'consumer.log').open('w');handles.append(f)
            children.append(subprocess.Popen([str(b/'framelink_av_consumer'),*common,'--stream','--audio-sink','fakesink'],stdout=f,stderr=subprocess.STDOUT))
            deadline=time.monotonic()+15
            codes=[c.wait(timeout=max(.01,deadline-time.monotonic())) for c in children]
            assert all(code!=0 for code in codes),(name,codes)
            assert not (session/'endpoint').exists(), 'stale live endpoint'
            # .owner.lock intentionally remains: unlinking would permit split ownership.
            results.append({'case':name,'exit_codes':codes,'seconds':time.monotonic()-started,'no_live_endpoint':True,'all_children_reaped':True})
        finally:
            for c in children:
                if c.poll() is None:c.kill();c.wait()
            for f in handles:f.close()
    (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results))

if __name__=='__main__':main()
