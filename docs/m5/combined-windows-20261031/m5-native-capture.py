import json,time,pathlib,urllib.request
out=pathlib.Path('C:/Users/jonsu/dev/m5-combined-native-window-status.json'); rows=[]
for i in range(200):
 try:
  with urllib.request.urlopen('http://127.0.0.1:18866/api/status',timeout=.2) as r: rows.append(dict(t=time.monotonic(),status=json.load(r)))
 except Exception: pass
 time.sleep(.1)
out.write_text(json.dumps(rows,indent=2))
print('Captured',len(rows),'native presentation status samples')
