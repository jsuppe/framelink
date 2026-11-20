#!/usr/bin/env python3
"""GPU-free checks of check_relay_trace.py on synthetic three-process traces."""
import os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_relay_trace as checker

COLUMNS = 'thread,operation,start_ns,duration_ns,channel_id,sequence,fence,slot,pts_ns,result,details,payload_bytes'


def write(directory, name, pid, rows, footer='dropped_events=0,rejected_threads=0,capacity_per_thread=4096'):
    with open(os.path.join(directory, name), 'w', encoding='utf-8') as f:
        f.write(f'# schema=2,clock=steady_ns,pid={pid},origin_ns=0,wall_ns=0\n{COLUMNS}\n')
        for op, details in rows:
            f.write(f'0,{op},0,0,0,0,0,0,0,0,{details},{64 if details & 1 else 0}\n')
        if footer:
            f.write(f'# {footer}\n')


def graph(directory, frames=4, relay_extra=(), footer=None, reads=None):
    write(directory, 'core.1.csv', 1, [('flAcquireBuffer', 0), ('flImageMapPlanes', 0), ('flEndSubmit', 0)] * frames)
    write(directory, 'core.2.csv', 2, [('reader.acquire', 0), ('flBeginSubmit', 0), ('flEndSubmit', 0),
                                       ('reader.complete', 0), ('reader.retire', 0)] * frames + list(relay_extra))
    n = frames if reads is None else reads
    write(directory, 'core.3.csv', 3, [('reader.acquire', 0), ('flImageMapPlanes', 0), ('reader.complete', 0), ('reader.retire', 0)] * n,
          footer=footer if footer is not None else 'dropped_events=0,rejected_threads=0,capacity_per_thread=4096')


def run(expect, **kw):
    with tempfile.TemporaryDirectory() as d:
        graph(d, **kw)
        s = checker.check(d, 4, 1)
        assert s['pass'] == expect, (kw, s['problems'])
        if s['pass']:
            roles = sorted(p['role'] for p in s['processes'].values())
            assert roles == ['reader', 'relay', 'source'], roles
        return s['problems']


run(True)
assert any('CPU image maps' in p for p in run(False, relay_extra=[('flImageMapPlanes', 0)]))
assert any('payload operations' in p for p in run(False, relay_extra=[('gst.cpu_copy_sink', 1)]))
assert any('lossy recorder footer' in p for p in run(False, footer='dropped_events=3,rejected_threads=0'))
assert any('lossy recorder footer' in p for p in run(False, footer=''))
assert any('final readers' in p for p in run(False, reads=3))
assert any('acquired' in p for p in run(False, relay_extra=[('reader.acquire', 0)]))
with tempfile.TemporaryDirectory() as d:
    assert not checker.check(d, 4, 1)['pass']  # no traces proves nothing
print('PASS: relay trace checker accepts a clean graph and rejects relay maps, payload, lossy/absent footers, missing reads and empty runs')
