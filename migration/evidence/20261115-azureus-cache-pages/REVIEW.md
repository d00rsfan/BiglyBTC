# Complete cache namespace in bounded pages

Native `93a6a3c29df3dbe8123348ca39b8b4835dbf2c9a`, tree `9d42aff6199b4957fd281a7ab7ce012891bce348`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
This supersedes the snapshot/value-packing limits of the preceding runtime milestone.

The DB freezes only keys and a store-epoch fence. Pages own at most 64 keys and 256
values; updates or recreations after the fence are skipped rather than mistaken
for a clock rollback. Expiry is scanned once per cycle, pages perform no ordinary
read/counter/clock callbacks, and the original actual-entry future-time repair is
preserved. Cycle IDs, cursor checks and owning key snapshots prevent invalidated
iterators and old-cycle reuse. A 30-minute job that times out resumes after its
last completely processed page on its next cycle, retaining unprocessed data.
The 8192-value snapshot refusal is removed; the existing 8192-key DB bound remains.

Every continuation carries a unique job-wide phase identity and checks bounds
before accessing a page slot. An old callback cannot authorise a different key
when a slot is empty or reused. New pages are queued through Qt instead of
recursing through synchronous callbacks. Per-page data is freed, and actual
anti-spoof tokens are reused only for the same endpoint/protocol/instance within
a bounded window. Source permits and current content/store receipts remain
mandatory before STORE and conditional purge.

Packet sizing uses the exact same encoder as ordinary STORE submission. A sizing
probe consumes no RNG, network, profile I/O or send accounting. Values for one key
are packed to the 1400-byte limit; actual submission still creates its normal
connection/transaction/header. This retires the fixed two-values-per-packet cap.
Four destination contacts remain concurrent. Multi-key packing/source batching
is a separate performance optimisation, not implied by this milestone.

Qualification covers 8385 empty remote values across 65 keys with bounded pages,
all values visited exactly once and one expiry clock sample. It also verifies
legitimate post-fence updates, exact-age handling, cycle rotation/supersession,
actual 65-key transfer across page boundaries, token reuse, local-only yielding,
late callbacks at empty/reused slots and timeout resumption at the next key.
Both IPv4 and IPv6 accept 16 short values in one packet per recipient, while the
512-byte-value boundary remains valid. Actual generated-profile forwarding and
DHT-config revocation passed. A test-fixture 32-key limit was corrected; the
production receipt-event budget was already adequate and was not changed.

The selected chat/NAT, private-torrent/lookup, original Java/native rating and
related-content, actual forced-seed Pause/Resume/Stop and shared STORE/checkpoint
regressions passed without skips. One exact-source application link passed;
[qualification.json](qualification.json) records totals. The unchanged 19-case
actual original reference was retained. Accepted runs use owned endpoints and
generated profiles. No daily image or live-profile acceptance was performed.

Cache paging/packing is qualified; broader F09 and the current task remain open
for explicit-replication survey execution and signed key-block maintenance.
