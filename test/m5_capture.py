"""Bounded finite-test stdout capture; disk writes occur only after child exit.

Not a transport fix or a performance qualification mode. Continuously drain
pipes even on overflow, then fail explicitly; never silently omit evidence.
"""
import subprocess
import threading


class CapturedProcess(subprocess.Popen):
    LIMIT = 4 * 1024 * 1024

    def __init__(self, *args, stdout, **kwargs):
        self._destination = stdout
        self._chunks = []
        self._overflow = False
        self._error = None
        self._saved = False
        super().__init__(*args, stdout=subprocess.PIPE, **kwargs)
        self._reader = threading.Thread(target=self._drain, daemon=True)
        self._reader.start()

    def _drain(self):
        size = 0
        try:
            while True:
                data = self.stdout.read(65536)
                if not data:
                    break
                size += len(data)
                if size <= self.LIMIT:
                    self._chunks.append(data)
                else:
                    self._overflow = True
        except Exception as error:
            self._error = error
        finally:
            self.stdout.close()

    def wait(self, timeout=None):
        code = super().wait(timeout=timeout)
        self._reader.join(timeout=5)
        if self._reader.is_alive():
            raise RuntimeError("stdout pipe remained open after child exit")
        if not self._saved:
            self._destination.write(b"".join(self._chunks).decode("utf-8", errors="replace"))
            self._destination.flush()
            self._chunks.clear()
            self._saved = True
        if self._error:
            raise RuntimeError("stdout capture failed") from self._error
        if self._overflow:
            raise RuntimeError("stdout capture exceeded 4 MiB; evidence incomplete")
        return code
