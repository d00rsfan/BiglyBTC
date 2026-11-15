# Source-owned signed Azureus key-block runtime

Native source `ebcc19570860fcaa4463bc2296c7025ac3aece84`, repository tree `cb3e63338d039d6a2a406f2aae19e525a254adf8`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Normal profile workers load original per-network dht/[netN]/block.dat with
block.saving fallback, verify the pinned official public key and transfer owning
state to the UDP actor. The fixed directory adapter retains the same acquired
profile, captured file identity/stamps and source/network; saves require an exact
preimage and absent pending publication, then exclusive .saving creation,
atomic rename, fsync and a fresh captured receipt. Unknown saved fields are
preserved by the qualified codec. Tests refuse changed/unsafe files, replaced
directories, pending data, stale preimages and other profile/network owners.

Actual request1036 handling checks source permit, endpoint identity, closest-K
and real IP token before signed-state admission. Changed data and duplicates
wait for a durable checkpoint; final source/record checks precede reply1037.
Up to32 jobs wait at most5s. Close, revoked sources, late callbacks and timeouts
cannot release a stale ACK. Frozen owning records are saved on the existing
profile broker, with coalesced revision waiters and bounded retry; no actor
filesystem work or extended shutdown wait is introduced.

Normal GET returns original error2 with the trusted signed metadata, including
a final check after delayed replies. STORE uses original admission/event order
and error2 semantics; the original store-before-block-detail sequence is
preserved, so this does not assert that blocked STORE never updates its cache.
Query-storage masks blocked mappings. Outbound original/cache/survey publication
refuses blocked keys, with current record checks before forwarding. Real
endpoint-correlated error2 replies learn indirect signed blocks under a fresh
source permit. Invalid, malformed and crypto-unavailable verification outcomes
are distinct. A new actual Java case confirms malformed signature length does
not poison the sender failure Bloom; malformed saved certificates are skipped,
while unavailable crypto still refuses unsupported restoration.

Direct block forwarding runs with the existing cache cadence, using actual
sorted/truncated closest-K and requiring local responsibility. It excludes the
original sender/already acknowledged contacts, gets an actual FIND_NODE token,
checks source and the same Record again, sends the signed request, then marks
sent only after its real correlated ACK and another source permit. Up to three
peers are active for one record. Each record uses a fresh token; this can send
more FIND_NODE requests than the original cache cycle's contact reuse. The
snapshot is bounded to8192 signed records/4MiB rather than value payloads; a
completed-key cursor gives later records a turn after a bounded cycle. Held
permits, withdrawal between token/send, timeout/late ACK and warm skip are tested.

Owned actual UDP tests exercise IPv4/IPv6 admission, duplicate waits, signed
forwarding and warm knowledge; the final wire check covers blocked GET/STORE/
QUERY_STORE without fabricating ordinary DB reads. Actual generated normal
profiles prove file publication before ACK, opaque-field retention, stop/join/
restart restoration, indirect learning and scheduled forwarding. Positive test
signatures use an explicit generated key available only to the owned-loopback
factory; normal public construction ignores it, and a saved file cannot select
a trust root. The official original modulus is1280 bits; the generated fixture
key is1536 bits. No official-private-key positive fixture or public-network
interoperability run is claimed. Earlier malformed-length and compile failures
remain unqualified; final accepted checks are listed in qualification.json.

One coherent GCC16 Release/C++23/Qt6.12.0 application link and affected chat,
torrent privacy, ratings, checkpoint, query/survey and forced-seed Pause/Stop
consumers pass. Pure original wire/state/planner proofs from earlier recorded
commits are carried, not rerun unless named. No live profile or daily image is
changed. Actual dynamic local-identity rebinding/direct-block purge integration
and the remaining wider F09 audit stay open; no full F09 baseline is advanced.
