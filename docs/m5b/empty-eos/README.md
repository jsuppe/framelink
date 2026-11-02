# Empty-stream terminal delivery

Reproduced: a negotiated stream ending before any PCM failed to deliver EOS remotely. GStreamer 1.24 gdppay releases initial event/header records only from its buffer chain.

Repair: after the authenticated terminal record, drain the GDP header queue with a private zero-size trigger. Drop its serialized BUFFER packet at the payloader output before transport; the receiver continues rejecting all zero-size PCM wire records. No silence/sample is fabricated or counted. If upstream provided no segment, create a canonical TIME segment solely for this empty stream. Nonempty streams are unchanged.

Audio-only and A/V zero-frame/zero-PCM sessions now complete. Linux affected 3/3 plus expanded A/V test passed, Windows 3/3 and Android 2/2 with G_DEBUG=fatal-warnings. Both native ABI and JNI builds passed. The audio test asserts no sample is delivered. Before-fix failure retained. Task5 final review and lifecycle gates remain open; no physical output claim.
