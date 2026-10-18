#!/usr/bin/env python3
"""A created/partially written ready file must not publish a truncated port."""

import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "migration/tools"))
from peer_interop import owned_peer_port


class Producer:
    def __init__(self, path, chunks):
        self.path, self.chunks, self.polls = path, iter(chunks), 0
        self.value = ""

    def poll(self):
        self.polls += 1
        self.value = next(self.chunks, self.value)
        self.path.write_text(self.value)
        return None


with tempfile.TemporaryDirectory(prefix="biglybtc-peer-readiness-") as directory:
    ready = pathlib.Path(directory) / "ready"
    producer = Producer(ready, ("", "123", "12345\n"))
    assert owned_peer_port(ready, producer, 2) == 12345
    assert producer.polls == 3
    for malformed in ("0\n", "65536\n", "1\n2\n", "no-port\n", "١٢\n"):
        try:
            owned_peer_port(ready, Producer(ready, (malformed,)), 1)
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed endpoint was accepted")
    try:
        owned_peer_port(ready, Producer(ready, ("",)), 0)
    except TimeoutError:
        pass
    else:
        raise AssertionError("Empty ready file did not respect its deadline")
print("PASS created-empty/partial/complete owned peer readiness and malformed bounds")
