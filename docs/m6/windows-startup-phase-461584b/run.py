#!/usr/bin/env python3
"""Explicit coordinator-only Windows diagnostic; no action on import."""
import argparse
import base64
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import uuid

ROOT = Path(__file__).resolve().parent
REV = '461584bbf5a6b8b70cec1578d69d35cb4b1ea2ff'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime-lane-released', action='store_true', required=True)
    p.add_argument('--host', default='<win-user>@<win-host>')
    p.add_argument('--repo', type=Path, default=Path('<linux-home>/dev/framelink-m6'))
    args = p.parse_args()
    manifest = json.loads((ROOT/'manifest.json').read_text())
    assert manifest['revision'] == REV
    assert sha(ROOT/'framelink_win.diagnostic.cpp') == manifest['diagnostic_source_sha256']
    assert sha(ROOT/'startup-phases.patch') == manifest['patch_sha256']
    tag = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex[:8]
    run = ROOT/'runs'/tag
    run.mkdir(parents=True, exist_ok=False)
    remote = '<win-home>/dev/framelink-startup-phase-461584b-'+tag
    state = dict(revision=REV, run_id=tag, remote=remote, status='preparing', scope='diagnostic; not acceptance')
    provisioned = False
    def record():
        (run/'status.json').write_text(json.dumps(state, indent=2)+'\n')
    def command(cmd, name, timeout=120):
        with (run/(name+'.log')).open('w') as output:
            return subprocess.run(cmd, stdout=output, stderr=subprocess.STDOUT, timeout=timeout).returncode
    def ps(code, name, timeout=120):
        (run/(name+'.ps1')).write_text(code)
        encoded = base64.b64encode(code.encode('utf-16le')).decode()
        if len(encoded) > 6000:
            assert provisioned, 'Long script requires provisioned directory'
            remote_script = remote+'/'+name+'.ps1'
            assert command(['scp','-q',str(run/(name+'.ps1')),args.host+':'+remote_script],name+'-script-copy') == 0
            return command(['ssh','-o','BatchMode=yes',args.host,'powershell','-NoProfile','-ExecutionPolicy','Bypass','-File',remote_script],name,timeout)
        return command(['ssh','-o','BatchMode=yes',args.host,'powershell','-NoProfile','-EncodedCommand',encoded],name,timeout)
    busy = r'''
function Assert-Idle {
 $active=@(Get-CimInstance Win32_Process | Where-Object {
  $_.Name -match '^(fl_.*|framelink.*|cmake|ctest|MSBuild)\.exe$' -or
  ($_.Name -match '^python(w|3)?\.exe$' -and $_.CommandLine -match 'm5_external_pipeline|validate_.*framelink')
 })
 if($active.Count){$active | Select-Object ProcessId,Name,CommandLine | Format-List;throw 'Windows runtime busy; nothing terminated'}
}
'''
    record()
    try:
        archive = run/'source.tar'
        subprocess.run(['git','-C',str(args.repo),'archive','--format=tar','-o',str(archive),REV],check=True)
        # Verify archived bytes, not the current working tree.
        with tarfile.open(archive) as tar:
            for name, expected in manifest['base_files'].items():
                assert hashlib.sha256(tar.extractfile(name).read()).hexdigest() == expected, name
        state['archive_sha256'] = sha(archive)
        state['diagnostic_sha256'] = manifest['diagnostic_source_sha256']
        record()
        setup = "$ErrorActionPreference='Stop'\n"+busy+f"\nAssert-Idle\nif(Test-Path '{remote}'){{throw 'Destination exists'}}\nNew-Item -ItemType Directory '{remote}' | Out-Null\n"
        assert ps(setup,'provision') == 0, 'Remote provision rejected'
        provisioned = True
        for path in [archive,ROOT/'framelink_win.diagnostic.cpp',ROOT/'manifest.json',ROOT/'startup-phases.patch']:
            assert command(['scp','-q',str(path),args.host+':'+remote+'/'+path.name],'copy-'+path.name) == 0
        state['status'] = 'remote-running'; record()
        code = "$ErrorActionPreference='Stop'\n$ProgressPreference='SilentlyContinue'\n"+busy+f'''
Set-Location '{remote}'
Assert-Idle
$mutex=New-Object System.Threading.Mutex($false,'Local\\FrameLinkFocusedWindowsValidation')
$locked=$mutex.WaitOne(0)
if(!$locked){{throw 'Windows lane mutex held'}}
$state=@{{revision='{REV}';status='starting';scope='diagnostic, not acceptance'}}
New-Item -ItemType Directory results | Out-Null
function Save-State {{$state | ConvertTo-Json -Depth 5 | Set-Content results/status.json}}
try {{
 Save-State
 if((Get-FileHash source.tar -Algorithm SHA256).Hash.ToLowerInvariant() -ne '{state['archive_sha256']}'){{throw 'Archive hash mismatch'}}
 if((Get-FileHash framelink_win.diagnostic.cpp -Algorithm SHA256).Hash.ToLowerInvariant() -ne '{manifest['diagnostic_source_sha256']}'){{throw 'Diagnostic hash mismatch'}}
 New-Item -ItemType Directory source | Out-Null
 tar -xf source.tar -C source
 if($LASTEXITCODE){{throw 'Archive extraction failed'}}
 if((Get-FileHash source/src/framelink_win.cpp -Algorithm SHA256).Hash.ToLowerInvariant() -ne '{manifest['base_files']['src/framelink_win.cpp']}'){{throw 'Base source mismatch'}}
 Copy-Item source/src/framelink_win.cpp results/framelink_win.base.cpp
 Copy-Item framelink_win.diagnostic.cpp source/src/framelink_win.cpp
 Copy-Item manifest.json,startup-phases.patch results/
 $env:PATH='C:/Program Files/gstreamer/1.0/msvc_x86_64/bin;'+$env:PATH
 Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber | ConvertTo-Json | Set-Content results/os.json
 Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID | ConvertTo-Json | Set-Content results/gpu.json
 Get-CimInstance Win32_Process | Select-Object ProcessId,Name | ConvertTo-Json | Set-Content results/processes-before.json
 & cmake --version *> results/cmake-version.log
 & gst-inspect-1.0 --version *> results/gstreamer-version.log
 $env:MSBUILDDISABLENODEREUSE='1'
 $state.status='building';Save-State
 & cmake -S source -B build -G 'Visual Studio 17 2022' -DFRAMELINK_BUILD_TESTS=ON -DFRAMELINK_BUILD_GST=ON -DFRAMELINK_BUILD_SAMPLES=ON *> results/configure.log
 $state.configureExit=$LASTEXITCODE;Save-State;if($LASTEXITCODE){{throw 'Configure failed'}}
 & cmake --build build --config Release --target framelink_av_producer framelink_av_consumer gstframelink -j 4 *> results/build.log
 $state.buildExit=$LASTEXITCODE;Save-State;if($LASTEXITCODE){{throw 'Build failed'}}
 Get-FileHash build/Release/framelink.dll,build/Release/framelink_gst_session.dll,build/Release/gstframelink.dll,build/Release/framelink_av_producer.exe,build/Release/framelink_av_consumer.exe -Algorithm SHA256 | Select-Object Path,Hash | ConvertTo-Json | Set-Content results/binaries.json
 Copy-Item build/CMakeCache.txt results/
 Get-ChildItem Env: | Where-Object {{$_.Name -like 'FRAMELINK_TEST*' -or $_.Name -like 'FRAMELINK_TRACE*' -or $_.Name -like 'FRAMELINK_GST_TRACE*' -or $_.Name -eq 'FRAMELINK_SETUP_PHASE_DIR'}} | ForEach-Object {{Remove-Item ('Env:'+ $_.Name)}}
 $env:PATH=(Resolve-Path build/Release).Path+';'+$env:PATH
 $env:GST_DEBUG='framelink:4'
 foreach($mode in @('audio','audio-coordinated')){{
  Assert-Idle
  $out=(New-Item -ItemType Directory ('results/'+$mode)).FullName
  $env:FRAMELINK_SETUP_PHASE_DIR=$out
  $env:FRAMELINK_TRACE=$out+'/native.csv'
  $env:FRAMELINK_GST_TRACE=$out+'/gst.csv'
  $state.status='running-'+$mode;Save-State
  # Recorder adds .PID.csv; each arm has its own directory. No overwritten shared trace.
  & python source/test/m5_external_pipeline.py --build build/Release --output $out --audio-sink fakesink --clock-policy $mode *> ('results/'+$mode+'-command.log')
  $state[$mode+'Exit']=$LASTEXITCODE;Save-State
  # A failure is evidence, not a retry request. Continue only after peers exited.
  Assert-Idle
 }}
 $state.status='finished'
}} catch {{
 $state.status='failed';$state.error=$_.Exception.Message
}} finally {{
 $state.finishedUtc=[DateTime]::UtcNow.ToString('o');Save-State
 if($locked){{$mutex.ReleaseMutex()}};$mutex.Dispose()
}}
if($state.status -ne 'finished'){{exit 1}}
exit 0
'''
        state['remote_exit'] = ps(code,'execute',900)
        state['status'] = 'remote-finished' if state['remote_exit'] == 0 else 'remote-failed'
    except Exception as error:
        state['status'] = 'failed'; state['error'] = str(error)
        # On timeout we never kill remote actors or start another arm ourselves.
        if isinstance(error,subprocess.TimeoutExpired):
            state['remote_may_still_run'] = True
    finally:
        if provisioned:
            try:
                state['pull_exit'] = command(['scp','-rq',args.host+':'+remote+'/results',str(run)],'pull-results',180)
            except Exception as error:
                state['pull_error'] = str(error)
        state['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat();record()
    if (run/'results').exists():
        remote_status = run/'results'/'status.json'
        if remote_status.exists():
            state['remote_status'] = json.loads(remote_status.read_text(encoding='utf-8-sig'))
        state['analysis_exit'] = command([sys.executable,str(ROOT/'analyze.py'),str(run/'results')],'analysis',60)
        if any(state.get('remote_status',{}).get(arm+'Exit') not in (None,0) for arm in ['audio','audio-coordinated']):
            state['status'] = 'completed-with-fixture-failures'
        record()
    print(json.dumps(state,indent=2))
    return 0 if state.get('remote_exit') == 0 and state.get('pull_exit') == 0 and state.get('analysis_exit') == 0 and state['status'] != 'completed-with-fixture-failures' else 1

if __name__ == '__main__':
    sys.exit(main())
