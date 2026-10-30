"""Two independently launched contract peers, no GPU/audio/media application.

The harness supplies pipe transport and fixed capability fixtures. It relays a
160-byte proposal and acknowledgement and checks both exit statuses. Production
rendezvous, offer codecs, authentication and endpoint ownership remain M3/M5.
"""
import subprocess
import sys
import threading

exe = sys.argv[1]
for case in ("agree", "unoffered", "stale", "changed-track", "unknown-clock", "malformed", "changed-ack"):
    proposer = subprocess.Popen([exe, "propose", case], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    packet = []
    reader = threading.Thread(target=lambda: packet.append(proposer.stdout.read(160)), daemon=True)
    reader.start()
    try:
        reader.join(5)
        if reader.is_alive() or len(packet[0]) != 160:
            raise RuntimeError(f"{case}: missing/bounded proposal")
        receiver = subprocess.run([exe, "accept", case], input=packet[0],
                                  capture_output=True, timeout=5)
        _, log = proposer.communicate(receiver.stdout, timeout=5)
        expected = (0, 0) if case == "agree" else (3, 0) if case == "changed-ack" else (2, 2)
        if (proposer.returncode, receiver.returncode) != expected:
            raise RuntimeError(f"{case}: exits {(proposer.returncode, receiver.returncode)}, expected {expected}: {log!r} {receiver.stderr!r}")
        if case == "agree" and receiver.stdout != packet[0]:
            raise RuntimeError("acknowledgement differs")
        print(f"{case}: proposer={proposer.returncode}, acceptor={receiver.returncode}; contract gate passed")
    finally:
        if proposer.poll() is None:
            proposer.kill()
            proposer.communicate()
        reader.join(1)
