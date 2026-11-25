# M6.4: delayed GPU completion on Linux and Android with a real GPU workload — 2026-11-25

Base revision: `c485ea44` plus this change (test only; no production code changed). This record is dated; current status is in [STATUS.md](../../../STATUS.md).

## Gap

On Windows, `read_lease_d3d11_delayed_completion` holds the consumer's GPU read behind a shared fence and shows that the read lease stays BUSY until real completion ([record](../d3d11-gpu-delay/README.md)). The Linux/Android counterpart, `read_lease_egl_delayed_completion`, gates the read behind an unsignalled EGL reusable sync. On both hosts it returns 77 (unavailable):
- On NVIDIA the exported native fence signals despite the gate ([negative record](../egl-injection-negative/README.md)).
- Mali offers no usable gate ([capability record](../egl-capability/README.md)).

M6.4's delayed-GPU subset was suspended "pending a supported native fence injection".

## New test: `read_lease_egl_gpu_busy_completion`

`fl_read_lease_egl --gpu-busy-completion` runs the same separate-process case as `read_lease_egl`:
- the producer fills and submits a frame, then waits up to 5 s to reacquire the leased slot;
- the consumer imports it through EGL, samples it with a draw, and completes its read lease with the native fence exported after that draw.

The difference: the sampling draw is queued behind a real GPU workload. That workload is a calibrated number of 256x256 draws of a 2048-step fragment loop, about 300 ms in total, each draw well under GPU watchdog limits. Blending is on, so tilers cannot discard overdrawn draws. The fence therefore signals late because the GPU is busy, not because of a host gate.

1. **Calibration and a raw control, before FrameLink is involved.** A native fence exported after the workload must stay unready for 100 ms. Otherwise the test returns 77, so it never passes vacuously.
2. **During the 100 ms after the workload is queued, every millisecond:**
   - the lease's fence fd is unready;
   - `flCollectReadLeases` returns BUSY;
   - the producer has not exited (it is blocked reacquiring the leased slot).
3. **Afterwards:** collection returns OK only once the fence fd is ready ("never retired before its fence"). The producer then reacquires and exits 0, and the read pixels are exact.

**Negative control** (`negative-control.diff`, not registered): the fence is exported *before* the workload, so it signals early. The test must then fail at step 2, and it does, 5 of 5 (`line 162: ready==0`). So the hold check detects a fence that precedes the GPU work.

## Results

| Host | GPU | Runs | Calibration | Raw fence ready after | Lease BUSY checks over 100 ms | Retired after workload queued |
|---|---|---|---|---|---|---|
| Linux, Ubuntu 26.04 | RTX 3090, NVIDIA 580.178.04 | 5/5 pass | 0.114–0.130 ms/draw, 2307–2631 draws | 270–302 ms | 87–89 | 264–306 ms |
| Android 15, TCL 8483A | Mali-G52 MC2 | 5/5 pass | 48.3–49.2 ms/draw, 7 draws | 328–329 ms | 81–82 | 329–331 ms |
| Linux negative control | RTX 3090 | 5/5 fail as required | | | | |

`fl_read_lease_egl` SHA-256 prefixes: Linux `1b012fc11f4dec32`, Android arm64 `2dcc6429221d3fb3`.

**Mali needed blending.** The first Android attempt, without blending, returned 77: a single draw took 49 ms, but seven opaque draws did not hold the fence for 100 ms. This is consistent with Mali discarding overdrawn fragments; the cause was not traced further. With blending on, all five runs passed.

## Regressions

- Linux full suite: 189/189, with the known skip `read_lease_egl_delayed_completion` included; the new test passes.
- Android: the `read_lease_egl` group 3 pass, 1 skip (the reusable-gate test), with the new test passing in the runs above.
- Windows: not built (EGL test).

## Scope

- **Not host-controlled.** The delay is a GPU workload of calibrated length, not a gate the host signals at a chosen moment, as the D3D11 test does. It shows that the lease, the slot and the producer are held while the GPU work is incomplete, and released only once the fence signals. It does not cover a GPU hang or device loss.
- **The reusable-gate test is unchanged** and still returns 77 on both hosts.
- **Whether this closes the suspended M6.4 delayed-GPU subset is Jon's decision.**

Files: `linux.log`, `android.log`, `linux-negative.log`, `negative-control.diff`.
