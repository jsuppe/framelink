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
required = {"memory_contract", "memory_contract_c_api", "audio_alignment", "gst_av_session", "gst_av_faults", "gst_timestamp_contract", "session_contract", "session_contract_c_api", "session_contract_peers", "av_auth", "av_rendezvous", "av_producer_help", "av_consumer_help", "m3_av_apps",
            "gst_session_transport", "gst_session_context", "gst_event_channel", "gst_audio_sink", "gst_audio_source",
            "gst_local_audio", "gst_av_timeline", "gst_av_validation", "gst_local_av", "gst_av_bin", "gst_av_src_bin",
            "gst_av_parity", "gst_av_qualification", "m5b_gst_av_example"}
required |= {"receive_queue", "read_leases", "read_ledger", "gst_pool_policy", "gst_policy_generation",
             "m4_transport_jitter_drift", "m4_transport_negative_retimestamps", "m5a_transport_relay_sync_negative",
             "trace_default_capacity", "trace_override_capacity", "trace_invalid_capacity",
             "trace_disabled", "trace_bounded_concurrent", "trace_thread_limit", "trace_details", "trace_compiled_out"}
missing = required - names
if missing:
    raise SystemExit("Missing required A/V tests: " + ", ".join(sorted(missing)))
print("Registered (not executed): " + ", ".join(sorted(required)))
