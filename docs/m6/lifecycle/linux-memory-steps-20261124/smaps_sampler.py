#!/usr/bin/env python3
"""Run one lifecycle cell and sample the long-lived peer's memory by kind.

Diagnostic for the Linux lifecycle memory steps: once a second, the
long-lived peer's /proc/PID/smaps_rollup (Rss split into anonymous, file and
shmem) and every mapping of 8 MB or more in /proc/PID/smaps (name, size,
Rss, shared/private), plus which fresh-peer cycle is running.

usage: smaps_sampler.py OUT_DIR BENCH PLUGIN BENCH_ARGS...
Writes OUT_DIR/bench.log, OUT_DIR/samples.jsonl. The bench runs as the
runner runs it (G_DEBUG=fatal-warnings).
"""
import json, os, re, subprocess, sys, time

out, exe, plugin, rest = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
exe = os.path.realpath(exe)  # children's argv[0] is the bench's absolute path
os.makedirs(out)
env = dict(os.environ, G_DEBUG='fatal-warnings')
os.makedirs(os.path.join(out, 'bench'))
log = open(os.path.join(out, 'bench.log'), 'w')
bench = subprocess.Popen([exe, plugin, '--out', os.path.join(out, 'bench'), '--label', 'cell'] + rest,
                         stdout=log, stderr=subprocess.STDOUT, env=env)
samples = open(os.path.join(out, 'samples.jsonl'), 'w')

def cmdline(pid):
    try:
        return open(f'/proc/{pid}/cmdline', 'rb').read().split(b'\0')
    except OSError:
        return []

def children():
    found = {}
    for pid in filter(str.isdigit, os.listdir('/proc')):
        a = cmdline(pid)
        if b'--role' in a and a[0] == exe.encode():
            role = a[a.index(b'--role') + 1].decode()
            sess = a[a.index(b'--session') + 1].decode() if b'--session' in a else ''
            found[int(pid)] = (role, sess)
    return found

def rollup(pid):
    r = {}
    for line in open(f'/proc/{pid}/smaps_rollup'):
        m = re.match(r'(\w+):\s+(\d+) kB', line)
        if m:
            r[m[1]] = int(m[2])
    return r

def big_maps(pid):
    maps, cur = [], None
    for line in open(f'/proc/{pid}/smaps'):
        m = re.match(r'([0-9a-f]+)-([0-9a-f]+) (\S+) \S+ \S+ \S+\s*(.*)', line)
        if m:
            size = (int(m[2], 16) - int(m[1], 16)) // 1024
            cur = {'start': m[1], 'kb': size, 'perms': m[3], 'name': m[4]} if size >= 8192 else None
            if cur:
                maps.append(cur)
        elif cur:
            k = re.match(r'(Rss|Anonymous|Shared_Clean|Shared_Dirty|Private_Clean|Private_Dirty):\s+(\d+) kB', line)
            if k:
                cur[k[1]] = int(k[2])
    return maps

t0 = time.monotonic()
while bench.poll() is None:
    kids = children()
    long = [p for p, (role, _) in kids.items() if role.startswith('lifecycle-')]
    fresh = [os.path.basename(os.path.dirname(s)) for p, (role, s) in kids.items() if not role.startswith('lifecycle-')]
    if long:
        try:
            rec = {'t': round(time.monotonic() - t0, 2), 'fresh': fresh, 'rollup': rollup(long[0]), 'maps': big_maps(long[0])}
            samples.write(json.dumps(rec) + '\n'); samples.flush()
        except OSError:
            pass
    time.sleep(1)
log.write(f'bench exit {bench.returncode}\n')
print('bench exit', bench.returncode)
