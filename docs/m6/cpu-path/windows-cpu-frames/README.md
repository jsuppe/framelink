# Windows CPU ready/release, metadata and EOS

Internal broker-bound SharedCpuControlWin now enforces exact generation, slot and
monotonic sequence on FCF1/FCR1 ready/release. A held slot returns BUSY; no overwrite,
copy fallback or policy-default change. CPU writes finish before submit and all
reads finish before release. Raw pointers remain caller-disciplined: this is not
a public opaque lease implementation, nor a native GPU ownership guarantee.

Both peers must explicitly agree to metadata and EOS bits. FCM1 preserves DTS,
duration, discontinuity and media generation. FCE1 carries the exact final sequence;
held frames remain readable and releasable after EOS. Empty streams and repeated
EOS observation are supported, disconnect is never synthesized as EOS. Unsupported
feature combinations reject without downgrade. POSIX public/wire APIs unchanged.

Windows Release validation: **5/5** affected tests pass. New control coverage:
- 100 child-process sessions (50 BGRA,50 NV12), each32 exact frames/pixel patterns,
  metadata and releases; every held slot rejects overwrite; final frame held across EOS.
- 57 setup negatives plus23 malformed/stale/replayed frame, metadata, release,
  disconnect and EOS cases; empty EOS and repeat observation.
- Exact process handle baseline restored after every case. This is not RSS slope,
  native D3D11 device lifecycle qualification or a full-resolution timing campaign.

Initial frame test rejected a fixture that supplied DTS/duration without presence
flags. Corrected fixture flags; original failure retained. No production behavior
was weakened. The final LastTest.log includes actual coverage output. Remote source
hashes match provenance.json; build uses staged c597cba+ changes on the b997e7c tree.
Next: public Windows CPU channel/delegation and authenticated coordinator binding.
All M6 unresolved acceptance gates and the pre-M7 maintenance hold remain.
