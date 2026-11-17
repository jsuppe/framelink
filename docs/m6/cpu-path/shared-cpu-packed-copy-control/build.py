from pathlib import Path
import subprocess,hashlib,json,shutil
root=Path(__file__).parent
old=Path('<linux-home>/dev/evidence/m6-cpu-path/android-residual-audit')
repo=Path('<linux-home>/dev/framelink-m6'); build=Path('<linux-home>/dev/build/framelink-m6-arm64')
pairs={'test/fl_gst_av_bench.cpp':'bench.cpp','gst/fl_gst_shared_cpu_sink.inc':'sink.inc','gst/fl_gst_shared_cpu_source.inc':'source.inc','gst/gstframelink.cpp':'gstframelink.cpp'}
saved={}
try:
 for name,artifact in pairs.items():
  p=repo/name;saved[p]=p.read_bytes();s=(old/artifact).read_text()
  if artifact=='source.inc':
   a='  for(guint y=0;y<rows;y++)memcpy(map.data+info.offset[p]+gsize(y)*info.stride[p],static_cast<const guint8*>(view.plane[p].pixels)+gsize(y)*view.plane[p].stride,rowBytes);'
   b='  if(g_getenv("FL_AUDIT_PLANE_COPY")&&view.plane[p].stride==rowBytes&&info.stride[p]==gint(rowBytes))memcpy(map.data+info.offset[p],view.plane[p].pixels,gsize(rows)*rowBytes);\n  else '+a.strip()
   assert s.count(a)==1;s=s.replace(a,b)
   a=' bool valid=view.planes==GST_VIDEO_INFO_N_PLANES(&info);guint64 bytes=0;'
   b=' if(seq==1)fprintf(stderr,"AUDIT plane_mode enabled=%d\\n",g_getenv("FL_AUDIT_PLANE_COPY")!=nullptr);\n'+a
   assert s.count(a)==1;s=s.replace(a,b)
  p.write_text(s);(root/artifact).write_text(s)
 with (root/'build.log').open('w') as log:subprocess.run(['cmake','--build',str(build),'-j4','--target','fl_gst_av_bench','fl_gst_shared_cpu_source','fl_gst_shared_cpu_roundtrip','fl_gst_shared_cpu_av'],stdout=log,stderr=subprocess.STDOUT,check=True)
 hashes={}
 for name in ['fl_gst_av_bench','fl_gst_shared_cpu_source','fl_gst_shared_cpu_roundtrip','fl_gst_shared_cpu_av']:
  shutil.copy2(build/name,root/name);hashes[name]=hashlib.sha256((root/name).read_bytes()).hexdigest()
 (root/'build.json').write_text(json.dumps({'base':'93283ac53d9a4d5caecf33a76704fe02085b909a','binary_sha256':hashes['fl_gst_av_bench'],'binaries':hashes,'sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in saved}},indent=2))
finally:
 for p,s in saved.items():p.write_bytes(s)
