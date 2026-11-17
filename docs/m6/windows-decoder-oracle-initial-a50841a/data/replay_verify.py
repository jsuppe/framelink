from pathlib import Path,PureWindowsPath
import hashlib,json,re
def collected_path(data, name):
 # Legacy Windows manifests used backslashes. Interpret both separator forms
 # explicitly; reject drives/UNC/rooted paths and traversal before resolution.
 if not isinstance(name,str) or not name or "\0" in name:
  raise RuntimeError('invalid collected manifest path')
 relative=PureWindowsPath(name)
 if relative.anchor or not relative.parts or any(part=='..' or ':' in part for part in relative.parts):
  raise RuntimeError('unsafe collected manifest path: '+name)
 root=data.resolve();path=root.joinpath(*relative.parts).resolve()
 if not path.is_relative_to(root) or path==root:
  raise RuntimeError('collected manifest path escapes root: '+name)
 return path
def verify_collection(data):
 manifest=json.loads((data/'collected-sha256.json').read_text());seen=set()
 if not isinstance(manifest,dict) or not manifest:raise RuntimeError('empty collection manifest')
 for name,digest in manifest.items():
  path=collected_path(data,name)
  if path in seen or not isinstance(digest,str) or not re.fullmatch('[a-f0-9]{64}',digest):raise RuntimeError('duplicate path or invalid collection digest')
  seen.add(path)
  if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError('collected hash mismatch: '+name)
 actual={p.resolve() for p in data.rglob('*') if p.is_file() and p!=data/'collected-sha256.json'}
 if actual!=seen:raise RuntimeError('collection manifest does not cover every raw artifact')

def zero(v):return type(v) is int and v==0

def build_prerequisite(controller,status):
 if controller.get('state')!='finished' or any(not zero(controller.get(k)) for k in ('remote_exit','package_exit','pull_exit')):raise ValueError('build controller not successfully collected')
 if status.get('state')!='finished' or any(not zero(status.get(k)) for k in ('build_exit','decoder-rate-smoke_exit','decoder-rate-analysis_exit','focused-tests_exit')):raise ValueError('build/focused/smoke checks did not pass')
 revision=controller.get('revision','')
 if not re.fullmatch('[a-f0-9]{40}',revision) or status.get('revision')!=revision:raise ValueError('build revision mismatch')
 binaries=status.get('binary_sha256',{});required={'fl_gst_native_producer_win.exe','fl_gst_local_decoder_rate_win.exe','gstframelink.dll','framelink.dll','framelink_gst_session.dll'}
 if set(binaries)!=required or not all(isinstance(h,str) and re.fullmatch('[a-f0-9]{64}',h) for h in binaries.values()):raise ValueError('five binary pins required')
 return revision,binaries

def analysis(result,trace):
 if result.get('pass') is not True or result.get('trace_checked') is not trace or result.get('trace_qualified') is not False:raise ValueError('pinned analyzer did not validate exact trace arm')

def tape_pin(pin,spec):
 if not isinstance(pin,dict) or pin.get('path')!=spec['fixture_path'] or type(pin.get('bytes')) is not int or pin['bytes']<=0 or not isinstance(pin.get('sha256'),str) or not re.fullmatch('[a-f0-9]{64}',pin['sha256']):raise ValueError('invalid tape identity')

def hash_file(path):
 h=hashlib.sha256();size=0
 with Path(path).open('rb') as f:
  while block:=f.read(1024*1024):h.update(block);size+=len(block)
 return {'bytes':size,'sha256':h.hexdigest()}

def file_pin(path,pin):
 if hash_file(path)!={k:pin[k] for k in ('bytes','sha256')}:raise ValueError('tape hash/size mismatch')

def profile(config,spec,trace):
 if any(config.get(k)!=spec[k] for k in ('seconds','width','height','fps','format')) or config.get('trace') is not trace:raise ValueError('capture profile/trace mismatch')
 if config.get('decoder_width')!=spec['width'] or config.get('decoder_height')!=spec['height']:raise ValueError('decoder profile mismatch')
 if config.get('tape_version')!=1 or config.get('tape_external_sha256_required') is not True:raise ValueError('missing external tape contract')

def preparation(root,spec,pin):
 tape_pin(pin,spec);root=Path(root);c=json.loads((root/'config.json').read_text());profile(c,spec,False)
 if c.get('fixture_mode')!='prepare-au-tape':raise ValueError('not preparation mode')
 r=json.loads((root/'fixture-prepared.json').read_text())
 if r!={'schema':1,'prepared':True,'encoded_aus':spec['seconds']*spec['fps'],'tape_bytes':pin['bytes'],'decoded_cache':False,'external_sha256_required':True}:raise ValueError('preparation summary mismatch')

def capture(config,spec,pin,trace):
 tape_pin(pin,spec);profile(config,spec,trace)
 f=config.get('decoder_fixture',{})
 if config.get('fixture_mode')!='replay-au-tape' or f.get('source')!='file-fed-appsrc' or f.get('generated_before_measurement') is not True or f.get('source_is_live') is not False:raise ValueError('not explicit pre-generated replay')
 if f.get('compressed_queue')!={'max_size_buffers':2,'max_size_bytes':0,'max_size_time_ns':0,'leaky':'no'}:raise ValueError('replay queue mismatch')

def remote_status(status,spec):
 if status.get('state')!='finished' or status.get('revision')!=spec['revision']:raise ValueError('remote outcome/revision mismatch')
 pin=status.get('fixture_pin');tape_pin(pin,spec)
 for phase in ('prepare','trace-off','trace-on'):
  for name in (phase,phase+'-idle-before',phase+'-idle-after'):
   if not zero(status.get(name+'_exit')):raise ValueError('missing successful phase/idle: '+name)
  if status.get(phase+'_binary_after')!=spec['binary_sha256']:raise ValueError('phase binary mismatch')
  if phase!='prepare':
   if status.get(phase+'_binary_before')!=spec['binary_sha256']:raise ValueError('phase initial binary mismatch')
   if not zero(status.get(phase+'-analysis_exit')):raise ValueError('phase analysis failed')
   if status.get(phase+'_fixture_before')!=pin or status.get(phase+'_fixture_after')!=pin:raise ValueError('phase tape changed')
 if status.get('binary_before')!=spec['binary_sha256']:raise ValueError('initial binaries mismatch')
 if status.get('fixture_observed_final')!=pin:raise ValueError('final tape changed')

# Independent streaming inspection: no GStreamer, decoder, or native runtime.
def inspect_tape(path,spec):
 import struct
 h=hashlib.sha256();total=0
 with Path(path).open('rb') as f:
  def take(n):
   nonlocal total
   data=f.read(n)
   if len(data)!=n:raise ValueError('truncated tape')
   h.update(data);total+=n;return data
  def u64():return struct.unpack('<Q',take(8))[0]
  header=[u64() for _ in range(9)];count=spec['seconds']*spec['fps']
  if header[:8]!=[0x3150415441554c46,1,count,spec['width'],spec['height'],spec['width'],spec['height'],spec['fps']]:raise ValueError('tape header/count/profile mismatch')
  caplen=header[8]
  if not 0<caplen<=65536:raise ValueError('invalid caps length')
  caps=take(caplen).decode('utf8')
  if '\0' in caps or not caps.startswith('video/x-h264') or not re.search(r'\balignment=\(string\)"?au"?(?:,|$)',caps):raise ValueError('invalid AU caps')
  origin=None;payload=0
  for index in range(count):
   ordinal,size,pts,dts,duration,flags,offset,offset_end=[u64() for _ in range(8)]
   if ordinal!=index or not 0<size<=64*1024*1024 or flags>0xffffffff:raise ValueError('invalid record ordinal/length/flags')
   if origin is None:origin=pts
   if pts==0xffffffffffffffff or pts<origin or pts-origin!=index*1000000000//spec['fps']:raise ValueError('invalid record timestamp')
   payload+=size
   while size:chunk=min(size,1024*1024);take(chunk);size-=chunk
  if [u64(),u64()]!=[0x31444e4555414c46,count] or f.read(1):raise ValueError('invalid footer/count/EOF')
 return {'pass':True,'bytes':total,'sha256':h.hexdigest(),'encoded_aus':count,'payload_bytes':payload,'caps':caps}
