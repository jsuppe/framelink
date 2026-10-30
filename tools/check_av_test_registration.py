"""Fail CI if optional GStreamer discovery silently omitted the A/V gates.

This checks test registration only. Running these tests requires native DRM or
D3D11 resources; hosted compile jobs do not claim hardware execution coverage.
"""
import json
import subprocess
import sys

command = ["ctest", "--test-dir", sys.argv[1], "--show-only=json-v1"]
if len(sys.argv) > 2:
    command += ["-C", sys.argv[2]]
result = subprocess.run(command, check=True, capture_output=True, text=True)
names = {test["name"] for test in json.loads(result.stdout)["tests"]}
required = {"gst_av_session", "gst_av_faults", "gst_timestamp_contract"}
missing = required - names
if missing:
    raise SystemExit("Missing required A/V tests: " + ", ".join(sorted(missing)))
print("Registered (not executed): " + ", ".join(sorted(required)))
