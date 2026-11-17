#!/usr/bin/env python3
"""Require every public FrameLink GStreamer element to load from a build tree.

Usage: check_gst_elements.py <build-dir>
Same on every platform: the plugin and framelink.dll/.so are both in <build-dir>.
"""
import os
import subprocess
import sys

ELEMENTS = ["framelinksink", "framelinksrc", "framelinkaudiosink", "framelinkaudiosrc",
            "framelinkavsink", "framelinkavsrc"]

build = os.path.abspath(sys.argv[1])
env = dict(os.environ, GST_PLUGIN_PATH=build)
# On Windows gstframelink.dll resolves framelink.dll through PATH.
env["PATH"] = build + os.pathsep + env.get("PATH", "")
missing = [e for e in ELEMENTS
           if subprocess.run(["gst-inspect-1.0", "--exists", e], env=env).returncode != 0]
if missing:
    sys.exit("missing elements: " + ", ".join(missing))
subprocess.run(["gst-inspect-1.0", "framelinkavsrc"], env=env, check=True)
print("all %d elements load" % len(ELEMENTS))
