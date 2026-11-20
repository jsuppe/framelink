#!/usr/bin/env python3
"""Check the FrameLink traces of one relay graph run (every process's
FRAMELINK_TRACE / FRAMELINK_GST_TRACE files in one directory).

Each process is classified by what it did:
  source  publishes frames, never acquires one
  relay   acquires frames AND publishes them (one per hop)
  reader  acquires frames, never publishes

Requirements:
  - every trace file has a lossless recorder footer and at least one row;
  - each relay forwarded exactly the frames it acquired, with no payload
    operation (details bit 1) and no CPU map of any image: the relay moves
    ownership only, never pixels;
  - the final readers acquired, completed and retired the expected frames.

FrameLink boundary instrumentation only: this does not prove anything about
driver-internal copies or physical memory traffic, and an absent or truncated
trace proves nothing (hence the footer and row checks).
"""
import argparse, collections, csv, glob, json, os, re, sys

MAPS = {'flImageMap', 'flImageMapPlanes'}
PUBLISH = {'flEndSubmit', 'flSubmitSyncFd'}


def load(path):
    lines = open(path, encoding='utf-8').readlines()
    footer = [re.search(r'dropped_events=(\d+),rejected_threads=(\d+)', l) for l in lines if l.startswith('#')]
    footer = [m for m in footer if m]
    pid = re.search(r'pid=(\d+)', ''.join(l for l in lines if l.startswith('#')))
    rows = list(csv.DictReader(l for l in lines if not l.startswith('#')))
    return footer, (pid.group(1) if pid else None), rows


def check(directory, frames, relays):
    problems, processes = [], collections.defaultdict(lambda: {'files': [], 'ops': collections.Counter(), 'payload': collections.Counter()})
    files = sorted(glob.glob(os.path.join(directory, '*.csv')))
    if not files:
        problems.append('no trace files')
    for path in files:
        footer, pid, rows = load(path)
        name = os.path.basename(path)
        if len(footer) != 1 or footer[0].groups() != ('0', '0'):
            problems.append(f'{name}: missing or lossy recorder footer')
        if not rows or any(None in r for r in rows):
            problems.append(f'{name}: empty or malformed trace')
        p = processes[pid or name]
        p['files'].append(name)
        for r in rows:
            p['ops'][r['operation']] += 1
            if int(r.get('details') or 0) & 1:
                p['payload'][r['operation']] += 1
    roles = collections.defaultdict(list)
    for pid, p in processes.items():
        acquired, published = p['ops']['reader.acquire'], sum(p['ops'][o] for o in PUBLISH)
        role = 'relay' if acquired and published else 'source' if published else 'reader' if acquired else 'other'
        p['role'] = role
        roles[role].append(pid)
    if len(roles['relay']) != relays:
        problems.append(f"expected {relays} relay process(es), found {len(roles['relay'])}")
    forwarded = 0
    for pid in roles['relay']:
        p = processes[pid]
        acquired, published = p['ops']['reader.acquire'], sum(p['ops'][o] for o in PUBLISH)
        forwarded += published
        if acquired != published:
            problems.append(f'relay {pid}: acquired {acquired} but published {published}')
        if p['payload']:
            problems.append(f"relay {pid}: payload operations {dict(p['payload'])}")
        maps = sum(p['ops'][o] for o in MAPS)
        if maps:
            problems.append(f'relay {pid}: {maps} CPU image maps')
    if relays and forwarded != frames:
        problems.append(f'relays forwarded {forwarded}, expected {frames}')
    read = sum(processes[pid]['ops']['reader.acquire'] for pid in roles['reader'])
    for kind in ('reader.acquire', 'reader.complete', 'reader.retire'):
        n = sum(processes[pid]['ops'][kind] for pid in roles['reader'])
        if n != frames:
            problems.append(f'final readers: {kind} {n}, expected {frames}')
    summary = {
        'directory': os.path.basename(os.path.normpath(directory)),
        'frames': frames, 'relays': relays, 'forwarded': forwarded, 'final_reads': read,
        'processes': {pid: {'role': p['role'], 'files': p['files'], 'payload_operations': dict(p['payload']),
                            'cpu_maps': sum(p['ops'][o] for o in MAPS),
                            'acquired': p['ops']['reader.acquire'], 'published': sum(p['ops'][o] for o in PUBLISH)}
                      for pid, p in sorted(processes.items())},
        'problems': problems, 'pass': not problems,
    }
    return summary


if __name__ == '__main__':
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('directory')
    a.add_argument('--frames', type=int, required=True, help='frames the relays forward in total, and the final readers read')
    a.add_argument('--relays', type=int, default=1, help='relay processes expected (one per two-hop graph in the run)')
    a.add_argument('--json', help='write the summary here')
    args = a.parse_args()
    s = check(args.directory, args.frames, args.relays)
    if args.json:
        json.dump(s, open(args.json, 'w'), indent=1)
    for p in s['problems']:
        print('FAIL', p)
    if s['pass']:
        print('PASS', f"{s['directory']}: {s['forwarded']} frames forwarded by {args.relays} relay(s) with no payload operations or CPU maps; {s['final_reads']} final reads")
    else:
        print('FAIL', f"{s['directory']}: {len(s['problems'])} problem(s)")
    sys.exit(0 if s['pass'] else 1)
