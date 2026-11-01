#!/usr/bin/env python3
"""Account a buffered crossapp audio fixture trace, not physical/acoustic output."""
import argparse
import json
import pathlib
import re


def check(text, frames, rate=48000, fps=30):
    lines = text.splitlines()
    assert lines, 'empty trace'
    footer = re.fullmatch(r'diagnostic_records=(\d+) dropped=(\d+)', lines[-1])
    assert footer and int(footer[1]) == len(lines)-1 and int(footer[2]) == 0, 'incomplete recorder'
    spans = [tuple(map(int, m)) for m in re.findall(r'audiobasesink: rendering at (\d+) (\d+)/(-?\d+)', text)]
    writes = [tuple(map(int, m)) for m in re.findall(r'audiobasesink: wrote (\d+) of (\d+)', text)]
    assert len(spans) == len(writes) == frames, 'missing render/write span'
    assert all(i > 0 and o > 0 for _, i, o in spans), 'non-forward resampling span'
    assert all(w == n for w, n in writes), 'incomplete PCM write'
    assert sum(i for _, i, _ in spans) == sum(w for w, _ in writes) == frames*rate//fps, 'PCM accounting mismatch'
    assert 'dropping late sample' not in text and 'dropping sample out of segment' not in text, 'sink rejected PCM'
    video = re.findall(r'video received=(\d+) pts=(\d+)', text)
    assert [(int(n), int(pts)) for n, pts in video] == [(n, n*1_000_000_000//fps) for n in range(frames)], 'video identity/PTS mismatch'
    return dict(frames=frames, render_calls=len(spans), pcm_input_samples=sum(w for w, _ in writes),
                negative_or_zero_spans=0, lost_records=0,
                phase_resyncs=text.count('Unexpected discontinuity'))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=pathlib.Path)
    p.add_argument('--frames', type=int, required=True)
    p.add_argument('--rate', type=int, default=48000)
    p.add_argument('--fps', type=int, default=30)
    a = p.parse_args()
    assert a.frames > 0 and a.rate > 0 and a.fps > 0
    print(json.dumps(check(a.trace.read_text(), a.frames, a.rate, a.fps), indent=2))
