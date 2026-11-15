# Local DB checkpoint and read receipts

Milestone source `0e3e2c3a870c10367ac41ba9f7aedc800fbcb1cd`, repository tree `02f1e22bb0e95a73faaccb956f23095f240b5ecb`. Chapter remains in progress;
normal runtime mode is unchanged until the durable-response bridge is qualified.
Original BiglyBT baseline is `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The serialized DB can hydrate original type/expiry seeds once, before values or
checkpoint observations. Actual Mapping policy transitions/removal advance a
revision; unchanged checkpoints reuse an immutable owning snapshot. Hydration
consumes no entropy. Original counters, bloom, clock samples and rediversification
without TTL refresh are preserved. Default absent-mode behaviour is unchanged.
Noncanonical DB keys refuse serialisable checkpoint export rather than becoming
incorrect20-byte file records.

Pure per-key content receipts distinguish replacement, withdrawal and record
recreation, while unrelated keys do not invalidate the captured value. Values
remain owning immutable copies. Observing a receipt does not re-read values or
increment read/store counters. Late seed restoration cannot reuse an already
observed revision. These are integration prerequisites; they do not themselves
grant source, file or endpoint authority.

The original Mapping deliberately caches its old wire diversification during a
statistics-only lazy expiry: payload type can be None while the header retains
the prior type until the next normal callback. Existing source-qualified behaviour
and test assertions are retained; no speculative Java bug fix was introduced.

[Qualification](qualification.json):22 affected DB QTest totals passed with no
skips/failures, including actual Mapping size/frequency and owned receiver cases.
New checks exercise immutable seed/transition snapshots, stale content after
replacement/withdrawal/recreation,100 pure observations, mode/closed guards and
late-seed rejection. An earlier coherent DB module batch and the8 local-file tests
passed before the read-receipt extension; unchanged file tests were not repeated.
Logs stay outside Git. No Host rebuild/image, live profile mutation or normal
activation occurred. The implementing agent also reviewed this milestone.
