"""Isolated GLib scratch allocation control, not transport qualification."""
import ctypes as c,json,os,time
from ctypes import wintypes as w
os.add_dll_directory(r'C:\Program Files\gstreamer\1.0\msvc_x86_64\bin')
g=c.CDLL(r'C:\Program Files\gstreamer\1.0\msvc_x86_64\bin\glib-2.0-0.dll')
class ByteArray(c.Structure): _fields_=[('data',c.c_void_p),('len',c.c_uint)]
g.g_byte_array_sized_new.argtypes=[c.c_uint];g.g_byte_array_sized_new.restype=c.POINTER(ByteArray)
g.g_byte_array_set_size.argtypes=[c.POINTER(ByteArray),c.c_uint];g.g_byte_array_set_size.restype=c.POINTER(ByteArray)
g.g_byte_array_unref.argtypes=[c.POINTER(ByteArray)];g.g_byte_array_unref.restype=None
class Counters(c.Structure):
 _fields_=[('cb',w.DWORD),('PageFaultCount',w.DWORD)]+[(x,c.c_size_t) for x in ['PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage']]
k=c.WinDLL('kernel32',use_last_error=True);p=c.WinDLL('psapi',use_last_error=True)
k.GetCurrentProcess.restype=w.HANDLE
p.GetProcessMemoryInfo.argtypes=[w.HANDLE,c.POINTER(Counters),w.DWORD]
def sample():
 x=Counters();x.cb=c.sizeof(x)
 if not p.GetProcessMemoryInfo(k.GetCurrentProcess(),c.byref(x),x.cb):raise c.WinError(c.get_last_error())
 return {'faults':x.PageFaultCount,'private':x.PrivateUsage,'working_set':x.WorkingSetSize}
n=3840*2160*3//2;src=c.create_string_buffer(n);c.memset(src,123,n)
def run(reuse):
 buf=None;times=[];faults=[]
 if reuse:
  buf=g.g_byte_array_sized_new(n);g.g_byte_array_set_size(buf,n);c.memmove(buf.contents.data,src,n)
 before=sample()
 for i in range(600):
  t=time.perf_counter_ns()
  if not reuse:buf=g.g_byte_array_sized_new(n);g.g_byte_array_set_size(buf,n)
  c.memmove(buf.contents.data,src,n)
  if not reuse:g.g_byte_array_unref(buf)
  times.append((time.perf_counter_ns()-t)/1e6)
 after=sample()
 if reuse:g.g_byte_array_unref(buf)
 a=sorted(times)
 return {'mode':'reuse_warm' if reuse else 'allocate_free','frames':len(a),'payload_bytes':n,'fault_delta':after['faults']-before['faults'],'before':before,'after':after,'ms':{'p50':a[299],'p95':a[569],'max':a[-1]}}
print(json.dumps({'scope':'GLib allocation plus contiguous payload copy only; not D3D11, scheduler or transport attribution','runs':[run(False),run(True),run(False),run(True)]},indent=2))
