# DXGI Trim teardown control — negative result

100 cycles, 5 seconds each, two BGRA1080p textures, cached GStreamer wrapping; create/destroy D3D11 device each cycle. After textures/wrapper release and Flush, call IDXGIDevice3::Trim before final device release. Post-five-cycle private-commit slope **35.761 KB/cycle**, above the unchanged 16 KB/cycle gate. Final device Release returned zero every cycle. Exit zero means probe execution succeeded, not resource qualification.

Compare raw device churn 31.225, wrapped churn 28.210, exclusive retained device -0.384 KB/cycle. Trim does not fix the reproduced growth. No production Trim or shared-device cache is justified by this result. Original Windows transport lifecycle failure 21.10 KB/cycle remains unqualified.

Next: measure the outstanding consumer-restart direction with unchanged production and thresholds, to scope whether a device-lifetime repair must cover both roles. This is coverage/attribution, not acceptance of the failed direction. No more equivalent teardown probes planned.
