#!/usr/bin/env python3
"""Require every public FrameLink GStreamer element to load from a build tree.

Usage: check_gst_elements.py <build-dir>
Same on every platform: the plugin and framelink.dll/.so are both in <build-dir>.
"""
import os
import shutil
import subprocess
import sys

ELEMENTS = ["framelinksink", "framelinksrc", "framelinkaudiosink", "framelinkaudiosrc",
            "framelinkavsink", "framelinkavsrc"]

build = os.path.abspath(sys.argv[1])
env = dict(os.environ, GST_PLUGIN_PATH=build)
# On Windows gstframelink.dll resolves framelink.dll through PATH.
env["PATH"] = build + os.pathsep + env.get("PATH", "")
# Resolve the tool against the same PATH: Windows process creation does not
# search a child environment's PATH, only the parent's.
inspect = shutil.which("gst-inspect-1.0", path=env["PATH"])
if not inspect:
    sys.exit("gst-inspect-1.0 not found on PATH")
missing = [e for e in ELEMENTS
           if subprocess.run([inspect, "--exists", e], env=env).returncode != 0]
if missing:
    sys.exit("missing elements: " + ", ".join(missing))
subprocess.run([inspect, "framelinkavsrc"], env=env, check=True)
print("all %d elements load" % len(ELEMENTS))
