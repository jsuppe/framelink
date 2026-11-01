#!/usr/bin/env python3
"""External producer deployment failures: bounded exit and rendezvous cleanup.
Leaves logs, not live child processes. No physical devices required.
"""
import argparse,json,os,pathlib,subprocess,time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    b=a.build.resolve(); ext='.exe' if os.name=='nt' else ''
    plugin=str(b/('gstframelink.dll' if ext else 'libgstframelink.so'))
    for path in (pathlib.Path(plugin), b/('framelink_av_producer'+ext), b/('framelink_av_consumer'+ext)):
        assert path.is_file(),f'missing build artifact: {path}'
    missing=(a.output.resolve()/'absent-input.avi').as_posix()
    assert not pathlib.Path(missing).exists(), 'missing-media fixture must not exist'
    cases={
      'missing-endpoint':None,
      'missing-plugin':'no_such_framelink_element ! @VIDEO_SINK@ audiotestsrc num-buffers=10 ! @AUDIO_SINK@',
      'unsupported-caps':'videotestsrc num-buffers=10 ! capsfilter caps="video/x-raw,format=I420" ! @VIDEO_SINK@ audiotestsrc num-buffers=10 ! @AUDIO_SINK@',
      'missing-media':f'filesrc location="{missing}" ! avidemux name=d d.video_0 ! jpegdec ! videoconvert ! videoscale ! videorate ! @VIDEO_SINK@ d.audio_0 ! audioconvert ! audioresample ! @AUDIO_SINK@'}
    results=[]
    for name,pipeline in cases.items():
        root=a.output/name;root.mkdir();session=root/'session'
        common=[plugin,'--session',str(session),'--format','BGRA','--frames','10','--timeout-ms','1000']
        children=[];handles=[];started=time.monotonic()
        try:
            if pipeline is not None:
                path=root/'source.pipeline';path.write_text(pipeline+'\n')
                f=(root/'producer.log').open('w');handles.append(f)
                children.append(subprocess.Popen([str(b/('framelink_av_producer'+ext)),*common,'--input-pipeline',str(path)],stdout=f,stderr=subprocess.STDOUT))
            f=(root/'consumer.log').open('w');handles.append(f)
            children.append(subprocess.Popen([str(b/('framelink_av_consumer'+ext)),*common,'--stream','--audio-sink','fakesink'],stdout=f,stderr=subprocess.STDOUT))
            deadline=time.monotonic()+15
            codes=[c.wait(timeout=max(.01,deadline-time.monotonic())) for c in children]
            assert all(code!=0 for code in codes),(name,codes)
            log=(root/('producer.log' if pipeline is not None else 'consumer.log')).read_text(errors='replace')
            if name=='missing-plugin': assert 'no_such_framelink_element' in log and 'no element' in log,log
            elif name=='unsupported-caps': assert "can't handle caps" in log or 'could not link' in log,log
            elif name=='missing-endpoint': assert 'deadline' in log or 'timeout' in log,log
            else:
                # A parse/link failure never exercises the missing input file.
                assert 'could not link' not in log and 'no element' not in log,log
                assert 'No such file' in log or 'Resource not found' in log or 'resource not found' in log,log
            assert not (session/'endpoint').exists(), 'stale live endpoint'
            # .owner.lock intentionally remains: unlinking would permit split ownership.
            results.append({'case':name,'exit_codes':codes,'seconds':time.monotonic()-started,'intended_failure_verified':True,'no_live_endpoint':True,'all_children_reaped':True})
        finally:
            for c in children:
                if c.poll() is None:c.kill();c.wait()
            for f in handles:f.close()
    (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(json.dumps(results))

if __name__=='__main__':main()
