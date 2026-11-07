# Controlled D3D11 queue delay — passed

Three affected tests pass: ordinary completion, producer death, and delayed completion. New test queues Wait(shared gate,1) before CopyResource and completion Signal, verifies completionvalue0, leasecollectionBUSY and producerprocessblocked over20one-ms observations, then signals the gate from an independent device on the same adapter. Lease retires, all importedpixelsmatch, producer exitsnormally. This is an actual queuedGPU dependency, not a GPUthroughput benchmark orphysicalpresentationmeasurement. No productionchange.
