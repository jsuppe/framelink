from pathlib import Path
import subprocess,json,base64,re
root=Path(__file__).parent
result={}
def run(name,args):
 with (root/(name+'.log')).open('w') as f:r=subprocess.run(args,stdout=f,stderr=subprocess.STDOUT,timeout=180)
 result[name]=r.returncode
 (root/'compat.json').write_text(json.dumps(result,indent=2))
 if r.returncode:raise RuntimeError(name)
run('linux-build',['cmake','--build','<linux-home>/dev/build/framelink-m6-linux','--target','fl_gst_session_context','fl_gst_session_transport','fl_gst_shared_cpu_av','-j','4'])
run('linux-tests',['ctest','--test-dir','<linux-home>/dev/build/framelink-m6-linux','-R','^(gst_session_context|gst_session_transport|gst_shared_cpu_av)$','--output-on-failure'])
code="""$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$dump=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe' | Select-Object -Last 1
& $dump.FullName /exports <win-home>/dev/framelink-m6-windows-30d7a53/build/Release/framelink_gst_session.dll
exit $LASTEXITCODE
"""
run('exports',['ssh','-o','BatchMode=yes','<win-user>@<win-host>','powershell','-NoProfile','-EncodedCommand',base64.b64encode(code.encode('utf-16le')).decode()])
old=Path('<linux-home>/dev/evidence/m6-cpu-path/windows-integrated-30d7a53/api-runtime.log').read_text()
def symbols(t):return set(re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(fl_gst_\w+)',t,re.M))
before=symbols(old);after=symbols((root/'exports.log').read_text())
result['export_names']={'retained':len(before&after),'removed':sorted(before-after),'added':sorted(after-before)}
assert before and not before-after
assert after-before=={'fl_gst_session_open_named_cpu_pipe'}
(root/'compat.json').write_text(json.dumps(result,indent=2));print(result)
