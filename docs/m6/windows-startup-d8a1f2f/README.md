# Windows integrated d8a1f2f: failure collected, not qualified

Exact revision `d8a1f2f177d10e6e1c8df108322d58d38b22fb7a`; clean build/API inspection exit0, all119 exports unchanged. Full suite **140/142**,594.71s, exit8. Ten subsequent diagnostic commands were **not reached**. Original run ended2026-11-12T02:57:08Z. No completed ccd0460 campaign was repeated.

Failed `m5_startup`: audio-coordinated BGRA delivered99/100, missing PTS1080000000 (sequence55). Failed `m5_preconnect`: ready NV12 delivered99/100, missing PTS1320000000 (sequence67). Both producer summaries report100 submitted/zero ring-full drops. Source sequence-gap messages independently confirm the missing frames. These are middle-of-stream losses, not evidence of a startup or terminal-drain failure. PCM remains100blocks/96000samples.

Original preconnect GST trace shows source create sequence66 ending693330531195200ns and next create sequence68 starting693330577319900ns: **46.1247ms outside srcCreate**. The missing frame reached the producer sink. Native default pending depth is1/latest-frame-wins; original capture lacks native arrival/replacement records, so it cannot establish why the downstream streaming thread did not re-enter create for46ms. Do not blame new session readiness polling or change queue/default/budgets without a matched control.

One bounded unchanged-binary native-trace control ran `test/m5_preconnect.py`, with FRAMELINK_TRACE and65536event capacity. Exit0/pull0 at02:59:16Z, normal and deliberate legacy-negative cases complete. It **did not reproduce the normal-path loss and does not waive the original failures**. Transport rows and expected legacy replacements are retained, not treated as normal-path regressions.

Raw original startup/preconnect directories and native-trace control: `<linux-home>/dev/evidence/m6-cpu-path/windows-startup-d8a1f2f/`; complete input hashes in manifest.sha256. Integrated build/API source archive/logs: sibling `windows-integrated-d8a1f2f/`.

Owner Evelyn. Next task remains this Windows timing/queue failure: correlate original source downstream gap with sample/appsink/logging and playback scheduling, design a bounded matched control before a repair. No blind full-suite retry, no enlarged buffer/default-policy waiver. Automatic two-hop native-bin qualification follows validated repair or explicit evidence-backed suspension. API candidate stays8b057a1/not frozen; D2/D3 unresolved, M6open/M7hold. No executor left running after this diagnostic.
