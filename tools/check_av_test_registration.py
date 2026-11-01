"""Fail CI if optional GStreamer discovery silently omitted the A/V gates.

This checks registration only. The suite includes CPU-only alignment/contract
gates and GPU tests requiring native DRM or D3D11 resources; hosted jobs run
only their explicit CPU whitelist, not hardware qualification.
"""
import json
import subprocess
import sys

command = ["ctest", "--test-dir", sys.argv[1], "--show-only=json-v1"]
if len(sys.argv) > 2:
    command += ["-C", sys.argv[2]]
result = subprocess.run(command, check=True, capture_output=True, text=True)
names = {test["name"] for test in json.loads(result.stdout)["tests"]}
required = {"audio_alignment", "gst_av_session", "gst_av_faults", "gst_timestamp_contract", "session_contract", "session_contract_c_api", "session_contract_peers", "av_auth", "av_rendezvous", "av_producer_help", "av_consumer_help", "m3_av_apps"}
missing = required - names
if missing:
    raise SystemExit("Missing required A/V tests: " + ", ".join(sorted(missing)))
print("Registered (not executed): " + ", ".join(sorted(required)))
