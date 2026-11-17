#!/usr/bin/env python3
"""Keep machine-local identity out of commits.

Git runs this as the `localenv` filter (see .gitattributes and
tools/setup_local_env.py). Values come from `local.env` at the checkout root,
which is gitignored; `local.env.example` lists the keys.

  clean  (working tree -> commit):   local values  -> placeholders
  smudge (commit -> working tree):   placeholders  -> local values

Placeholders: <linux-home> <linux-host> <win-home> <win-user> <win-host>.
<win-home> keeps the separator that follows it, so C:\\Users\\x\\, C:\\Users\\x\\
and C:/Users/x/ all round-trip. Binary content (any NUL byte) passes through.
Without local.env both directions pass content through unchanged, and
tools/check_local_identity.py is the backstop.

Usage: localenv_filter.py clean|smudge   (stdin -> stdout)
       localenv_filter.py process        (git long-running filter protocol)
"""
import os
import re
import subprocess
import sys

KEYS = ("FL_LINUX_HOME", "FL_LINUX_HOST", "FL_WIN_HOME", "FL_WIN_USER", "FL_WIN_HOST")


def _root():
    try:
        return subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return os.getcwd()


def load_env(path=None):
    path = path or os.path.join(_root(), "local.env")
    env = {}
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip('"').strip("'")
    except FileNotFoundError:
        pass
    return {k: v for k, v in env.items() if k in KEYS and v}


class Filter:
    def __init__(self, env):
        self.env = env
        e = {k: v.encode() for k, v in env.items()}
        clean, smudge = [], []
        wh = e.get("FL_WIN_HOME")
        if wh:
            # Store C:\Users\x however it was written: escaped, backslash or slash.
            back = wh.replace(b"/", b"\\")
            parts = back.split(b"\\")
            esc = b"\\\\".join(parts)
            fwd = b"/".join(parts)
            for form in (esc, back, fwd):
                clean.append((re.compile(re.escape(form) + rb"(?![A-Za-z0-9_.-])"), b"<win-home>"))
            smudge += [(re.compile(rb"<win-home>(?=\\\\)"), esc),
                       (re.compile(rb"<win-home>(?=/)"), fwd),
                       (re.compile(rb"<win-home>"), back)]
        lh = e.get("FL_LINUX_HOME")
        if lh:
            clean.append((re.compile(re.escape(lh) + rb"(?![A-Za-z0-9_.-])"), b"<linux-home>"))
            smudge.append((re.compile(rb"<linux-home>"), lh))
        wu, whost = e.get("FL_WIN_USER"), e.get("FL_WIN_HOST")
        if wu and whost:
            clean.append((re.compile(rb"\b" + re.escape(wu) + rb"@" + re.escape(whost) + rb"\b"),
                          b"<win-user>@<win-host>"))
        for key, ph in (("FL_WIN_HOST", b"<win-host>"), ("FL_WIN_USER", b"<win-user>"),
                        ("FL_LINUX_HOST", b"<linux-host>")):
            v = e.get(key)
            if v:
                clean.append((re.compile(rb"\b" + re.escape(v) + rb"\b"), ph))
                smudge.append((re.compile(re.escape(ph)), v))
        self._clean, self._smudge = clean, smudge

    @staticmethod
    def _apply(rules, data):
        if not rules or b"\0" in data:
            return data
        for rx, rep in rules:
            data = rx.sub(lambda _m, r=rep: r, data)
        return data

    def clean(self, data):
        return self._apply(self._clean, data)

    def smudge(self, data):
        return self._apply(self._smudge, data)


# ---- git long-running filter protocol (gitprotocol-common pkt-line) ----------

def _read_pkt(f):
    head = f.read(4)
    if not head:
        return None
    n = int(head, 16)
    if n == 0:
        return b""          # flush
    return f.read(n - 4)


def _write_pkt(f, data):
    f.write(b"%04x" % (len(data) + 4) + data)


def _flush(f):
    f.write(b"0000")


def _read_text_list(f):
    out = []
    while True:
        p = _read_pkt(f)
        if p is None or p == b"":
            return out
        out.append(p.rstrip(b"\n").decode())


def process(flt):
    r, w = sys.stdin.buffer, sys.stdout.buffer
    if _read_text_list(r) != ["git-filter-client", "version=2"]:
        sys.exit("localenv_filter: unexpected handshake")
    for line in ("git-filter-server", "version=2"):
        _write_pkt(w, line.encode() + b"\n")
    _flush(w)
    w.flush()
    _read_text_list(r)
    for line in ("capability=clean", "capability=smudge"):
        _write_pkt(w, line.encode() + b"\n")
    _flush(w)
    w.flush()
    while True:
        meta = _read_text_list(r)
        if not meta:
            return
        cmd = dict(x.split("=", 1) for x in meta).get("command")
        chunks = []
        while True:
            p = _read_pkt(r)
            if p is None or p == b"":
                break
            chunks.append(p)
        data = b"".join(chunks)
        out = flt.clean(data) if cmd == "clean" else flt.smudge(data) if cmd == "smudge" else None
        if out is None:
            _write_pkt(w, b"status=error\n"); _flush(w); w.flush()
            continue
        _write_pkt(w, b"status=success\n"); _flush(w)
        for i in range(0, len(out), 65516):
            _write_pkt(w, out[i:i + 65516])
        _flush(w); _flush(w)          # end of content, unchanged status
        w.flush()


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    flt = Filter(load_env())
    if mode == "process":
        process(flt)
    elif mode in ("clean", "smudge"):
        data = sys.stdin.buffer.read()
        sys.stdout.buffer.write(getattr(flt, mode)(data))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
