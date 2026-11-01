# Transactional Windows ring import

Caspar 15/15 affected tests passed. The new private-backend test injects 24 failed
imports: invalid second texture, invalid ready fence, and texture/descriptor size
mismatch. Every received handle is closed; original ring pointers/generation stay
intact; the old ring still transfers a frame afterward. Initial attach and resize
now share the same transactional importer.

Causal control: the identical test against 184f915 fails at original-ring retention.
The first restore kept source timestamps, so MSBuild reused the negative executable
and failed again. That is retained in negative.log.gz, not called a production
failure. After touching source/test and rebuilding, the restored final source passes
all 24 injections (restored.log.gz). Full affected matrix is windows.log.gz.

This tests received-handle cleanup and atomic replacement, not failed remote
DuplicateHandle delivery or all hostile descriptors. Those are separate concerns.
