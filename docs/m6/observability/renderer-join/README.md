# Legacy renderer boundary join

Independent expected300 source PTS values at60fps join producer submission, successful receiver acquisition, adoption and presentation submission by channel/fence/PTS.11041 repeated render serials match render/present/GPU-completion event sets; these are not11041 fresh frames. All recorder footers report zero loss. CPU timestamp ordering through presentation submission passes.

Read-only inspection of the Windows fixture source shows GPU-completion events emitted after successful vkWaitForFences or shutdown vkDeviceWaitIdle (source-audit.log). The fixture is a source snapshot, not a Git checkout; binary-to-source provenance is not fully established here. Event-name/serial matching must not be represented as independently verified GPU duration. No physical scanout measurement.

M6.6a remaining dependency: current coordinated-session native producer/renderer integration in M6.9 is not yet available for this full joined gate. Native boundary GPU/PCM tests on Linux/Android/Windows pass separately. Legacy100ms hidden-display fixture is not the primary zero-added-delay renderer gate. Owner Evelyn; resume joined qualification on M6.9 integrated artifacts, preserving source-to-binary provenance and separate PCM/offset checks. M6.6a remains unchecked; M6.5c original stall causality remains unresolved.
