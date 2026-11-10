#!/usr/bin/env python3
"""Fail if tracked text files contain a personal home directory or the values in local.env.

Generic rule (works in CI without local.env): /home/<name> and C:\\Users\\<name>
in any spelling, except placeholders and known public build paths. With local.env
present, its exact user and host names are checked too.

  python3 tools/check_local_identity.py   # checks staged/committed content, not the working tree
"""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
from localenv_filter import load_env

ALLOW_LINUX = {"you", "runner", "nirbheek", "user", "ubuntu", "builder", "<linux-home>"}
ALLOW_WIN = {"you", "x", "runneradmin", "public", "default", "<win-user>"}
HOME = re.compile(rb"/home/([A-Za-z0-9_.-]+)")
WIN = re.compile(rb"[A-Za-z]:(?:\\\\|\\|/)Users(?:\\\\|\\|/)([A-Za-z0-9_.-]+)", re.I)

def main():
    # Check what is committed (the index), never the working tree: in a configured
    # checkout the localenv filter deliberately restores real values there.
    ls = subprocess.run(["git", "ls-files", "-s", "-z"], capture_output=True, check=True).stdout
    entries = [(l.split(b"\t", 1)[0].split()[1], l.split(b"\t", 1)[1]) for l in ls.split(b"\0") if l]
    exact = [re.compile(rb"\b" + re.escape(v.encode()) + rb"\b")
             for k, v in load_env().items() if k in ("FL_WIN_USER", "FL_WIN_HOST", "FL_LINUX_HOST")]
    cat = subprocess.Popen(["git", "cat-file", "--batch"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    bad = 0
    for sha, f in entries:
        cat.stdin.write(sha + b"\n"); cat.stdin.flush()
        hdr = cat.stdout.readline().split()
        if len(hdr) < 3 or hdr[1] != b"blob":
            continue
        data = cat.stdout.read(int(hdr[2])); cat.stdout.read(1)
        if b"\0" in data:
            continue
        hits = [m.group(0) for m in HOME.finditer(data) if m.group(1).decode().lower() not in ALLOW_LINUX]
        hits += [m.group(0) for m in WIN.finditer(data) if m.group(1).decode().strip().lower() not in ALLOW_WIN]
        hits += [m.group(0) for rx in exact for m in rx.finditer(data)]
        if hits:
            bad += 1
            print(f"{f.decode()}: {len(hits)} e.g. {hits[0].decode(errors='replace')}")
    if bad:
        sys.exit(f"{bad} file(s) contain local identity; run tools/setup_local_env.py and re-add them")
    print("no local identity in tracked files")


if __name__ == "__main__":
    main()
