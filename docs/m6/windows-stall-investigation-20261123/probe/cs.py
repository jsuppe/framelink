import sys
f,t=sys.argv[1],int(sys.argv[2])
# CSwitch: ts, NewProc, NewTID, NPri, NQnt, TmSinceLast, WaitTime, OldProc, OldTID, OPri, OQnt, OldState, WaitReason, Swapable, InSwitchTime, CPU,...
for line in open(f,errors='replace'):
  s=line.lstrip()
  if not s.startswith('CSwitch,'): continue
  p=[x.strip() for x in s.split(',')]
  try: ts=int(p[1])
  except: continue
  if not (t-100000<ts<t+250000): continue
  if 'fl_gst_local' in p[2] and 30000<int(p[7])<5000000:
    print(f'{(ts-t)/1000:8.1f}ms IN  {p[2]} tid {p[3]} pri {p[4]} waited {int(p[7])/1000:.1f} ms (switched in on cpu {p[16]})')
  if 'fl_gst_local' in p[8] and int(p[15])>30000:
    print(f'{(ts-t)/1000:8.1f}ms OUT {p[8]} tid {p[9]} ran {int(p[15])/1000:.1f} ms then {p[12]}/{p[13]}')
