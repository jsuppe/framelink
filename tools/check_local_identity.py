#!/usr/bin/env python3
"""Fail if tracked text files contain a personal home directory or the values in local.env.

Generic rule (works in CI without local.env): /home/<name> and C:\\Users\\<name>
in any spelling, except placeholders and known public build paths. With local.env
present, its exact values (users, hosts) are checked too.

  python3 tools/check_local_identity.py [--cached]   # --cached checks the index (pre-commit)
"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
from localenv_filter import load_env

ALLOW_LINUX = {"runner", "nirbheek", "user", "ubuntu", "builder", "<linux-home>"}
ALLOW_WIN = {"runneradmin", "public", "default", "all users", "<win-user>"}
HOME = re.compile(rb"/home/([A-Za-z0-9_.-]+)")
WIN = re.compile(rb"[A-Za-z]:(?:\\\\|\\|/)Users(?:\\\\|\\|/)([A-Za-z0-9_. -]+)", re.I)

def main():
    cached = "--cached" in sys.argv
    files = subprocess.run(["git", "ls-files", "-z"] + (["--cached"] if cached else []),
                           capture_output=True, check=True).stdout.split(b"\0")
    exact = [re.compile(rb"\b" + re.escape(v.encode()) + rb"\b")
             for k, v in load_env().items() if k in ("FL_WIN_USER", "FL_WIN_HOST", "FL_LINUX_HOST")]
    bad = 0
    for f in filter(None, files):
        data = (subprocess.run(["git", "show", b":" + f], capture_output=True).stdout if cached
                else open(f, "rb").read() if os.path.isfile(f) else b"")
        if b"\0" in data:
            continue
        hits = [m.group(0) for m in HOME.finditer(data) if m.group(1).decode().lower() not in ALLOW_LINUX]
        hits += [m.group(0) for m in WIN.finditer(data) if m.group(1).decode().strip().lower() not in ALLOW_WIN]
        if cached:
            hits += [m.group(0) for rx in exact for m in rx.finditer(data)]
        if hits:
            bad += 1
            print(f"{f.decode()}: {len(hits)} e.g. {hits[0].decode(errors='replace')}")
    if bad:
        sys.exit(f"{bad} file(s) contain local identity; run tools/setup_local_env.py and re-add them")
    print("no local identity in tracked files")

if __name__ == "__main__":
    main()
