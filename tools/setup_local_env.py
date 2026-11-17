#!/usr/bin/env python3
"""One-time setup per clone: create local.env and register the localenv filter.

  python3 tools/setup_local_env.py            # writes local.env from the example if absent
  python3 tools/setup_local_env.py --check    # report whether the filter is active

local.env is gitignored. Edit it to this machine's values; commits then carry
placeholders (<linux-home>, <win-home>, <win-user>, <win-host>, <linux-host>).
"""
import os, shutil, subprocess, sys

def git(*a, check=True):
    return subprocess.run(["git", *a], capture_output=True, text=True, check=check).stdout.strip()

root = git("rev-parse", "--show-toplevel")
env, example = os.path.join(root, "local.env"), os.path.join(root, "local.env.example")
if "--check" in sys.argv:
    proc = git("config", "--get", "filter.localenv.process", check=False)
    print("filter:", proc or "NOT CONFIGURED"); print("local.env:", "present" if os.path.exists(env) else "MISSING")
    sys.exit(0 if proc and os.path.exists(env) else 1)
if not os.path.exists(env):
    shutil.copyfile(example, env); print("created local.env from local.env.example; edit it for this machine")
script = "python3 tools/localenv_filter.py"
git("config", "filter.localenv.process", script + " process")
git("config", "filter.localenv.clean", script + " clean")
git("config", "filter.localenv.smudge", script + " smudge")
git("config", "filter.localenv.required", "true")
print("localenv filter registered in", git("rev-parse", "--git-common-dir"))
