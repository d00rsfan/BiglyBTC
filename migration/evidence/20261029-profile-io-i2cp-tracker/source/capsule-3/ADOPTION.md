# Explicit I2CP UDP tracker bridge — source only

Twelve source leaves and two CMake append sections, over the exact production
bytes of `61d1b53b07be81be7a98eaabe40794d8185fbfbe`. No native checkout,
provider, shared registry, profile or distribution changes were made. No C++ or
Java subject, router or endpoint was run. Existing receive-test formatting fix
`7c33` / native `9fca` is retained as an unchanged composition input.

The useful slice is `InitialChannel::scrapeTracker(TrackerRequest)`: one actual
hash lookup, signed Datagram2 BEP15 connect, then claimed Datagram3 single-hash
scrape, through the already explicitly configured loopback I2CP TCP socket.
This is an operational dispatch/receive seam, not merely DTOs. It never selects,
starts or installs a router. No Host, automatic service, saved torrent admission,
tracker/rank injection, public network or Streaming is enabled. Existing native
I2P/SAM admission fences are unchanged.

## Original mapping and strict scope

`source-pins.json` records eight exact official source-JAR/Git blobs at I2P
`9134f808337b401e8e53c73734c81fab04280c9d` and two exact plugin blobs at
`33d97eca67a9f332e3d4da7e7492ec54f4bec078`. Missing official blobs were retained
only after exact source-JAR bytes matched the retained tree object IDs.

Original `I2PSMHolder.lookupAddress/lookupDest` resolves a full Destination.
This first subset supports only HostLookup38 LOOKUP_HASH0 / default HostReply39,
with same-session nonreused request IDs and exact full Destination hash binding.
No hostname, address book, OS DNS, encrypted/b33 lookup/options or reply options
are supported. Unsupported signing/encryption Destination formats remain refused
by the existing decoder. Router lookup failure is separate from local timeout.

Original `I2PHelperSocksProxy.UDPImpl` uses source port9000, first Datagram2,
Datagram3 after an actual response, and `setSendLeaseSet(proto==Datagram2)`.
Its `SMHolderWrapper` listens for RAW18 replies. The bridge mirrors that sequence
for connect then scrape, with default flags0 on connect and the exact original
`SendMessageOptions.LS_MASK=0x0100` on the claimed scrape. Only these Expires36
flags are supported; existing flags0 behavior stays identical. I2CP nonce0 sends
are untracked router submissions, including initial reliability=none. A router
acceptance/status is never substituted for an actual BEP15 result.

Raw replies have **no authenticated sender identity**. They are confined to the
current actual-dispatched tracker operation and explicit destination hash/port,
local port9000, current session/owned signer/full local Destination/LS2/generation,
random transaction and connect-derived connection token. They cannot enter the
general signed2/claimed3 queue. The ordinary mux decoder still rejects RAW18;
the separately named raw tracker decoder has a 1024-byte decoded limit and
retains the existing gzip CRC/ISIZE, fixed-header and total wire bounds.

The BEP15 wire namespace body is moved byte-for-byte into SDK-neutral core header
`biglybtc/udp_tracker_wire.hpp`; the engine retains its old namespace alias and
unchanged `LibraryUdpTracker` body. Local C++ type names now resolve to the core
namespace, so affected native consumers must rebuild. No provider/public
libtorrent ABI or artifact changes occur. The new core header is an additive
native header; there is no new third-party dependency or duplicated parser.

## Bounds and lifecycle

One operation, one lookup, two wire transactions, one prepared bounded write and
one absolute deadline (1..60000ms, default30000; lookup request cap30000).
No automatic retries, reconnects, fallback or timeout extensions. A connect reply
must actually match before constructing/sending the scrape with its returned
connection token. Counts, correlated tracker error, malformed correlated reply,
lookup failure and local cancel/timeout/lease expiry remain different results.
Raw signed32 count fields are observations, not automatic ranking authority.

Random transaction pairs are checked against a fixed lifetime history: maximum
1024 operations /2048 wire transaction IDs (8KiB). IDs are never reused after
cancel, timeout or explicit channel restart; exhaustion refuses a new operation.
No hidden channel recreation clears this bound. Lookup and public operation IDs
also never reset within the object. Late or malformed old lookup replies are
correlated before parsing, and late raw replies cannot finish a later operation.

The existing bounded legacy Begin/End fetch machinery serves the explicit tracker
even when the general receiver is disabled. General receiver disable does not
cancel a separately authorized tracker fetch. Tracker cancellation releases a
pending fetch when no general receiver remains. End is only router storage
release. Writes wait for the existing socket/control buffer to drain, are freshly
checked before dispatch, and cannot grow an unbounded Qt write queue. Reconfigure
and ordinary tracked sends refuse while this explicit operation is pending.
Lease renewals do not reset its deadline. Close/terminal clears round/results,
while lifetime ID history remains. Notifications are queued, generation-tagged,
QPointer guarded and safe for synchronous listener deletion.

## Qualification to execute later

Append `CMake.fragment` services/tests sections once. New targets/CTest names:

- `biglybtc-i2cp-tracker-codec-tests` / `pure-i2cp-tracker-objects` (60s).
- `biglybtc-i2cp-tracker-channel-tests` / `i2cp-explicit-tracker-channel` (60s).

Run affected existing I2CP/channel/LS2/receive/datagram/session suites and
`mainline-single-udp` for the shared model extraction. All existing test files
are untouched. Owned TCP tests exercise actual greeting/date, owned signed
registration/LS2, lookup, independently parsed BEP15 connect/scrape, signed2
verification, claimed3/skip-LS flag, RAW18 delivery and counts over IPv4/IPv6,
both legacy and fast mode. They also exercise stale/malformed/wrong-ID lookup,
wrong port/transaction, cancel/late raw replies, actual deadline, missing LS2,
lookup failure/hash mismatch and callback deletion. The fixture impersonates an
I2CP router and tracker; it does **not** prove a real router's tunnels or routing.

`OFFLINE-COMMANDS.sh FRESH_ABSOLUTE_OUTPUT NATIVE_CODEC_ELF SYNTHETIC_FIXTURES
OFFICIAL_JAR MODIFIED_JAR` compiles one actual Java object helper, then exchanges
two signing-family cases in both directions with each exact pinned JAR. Each
case covers Host38/39, both actual Expires36 options and signed2/claimed3 packets,
and a RAW18 Payload31. Root should bind current source/ELF hashes, exact class-load
`file:/` JAR origins, two labels per direction/reference and a 180s outer budget.
DSA signatures are verified, not compared for random-byte equality. Modified
whole-core/source equivalence remains open.

A subsequent **separately authorized** real-router acceptance can use one fresh
owned, isolated router and two synthetic destinations with zero-hop tunnels:
actual native I2CP client → original Java tracker responder → actual native
counts, then cancel/restart/failure controls. Before executing, qualify the
router's exact pinned configuration that disables public transports/reseed and
selects only the fresh loopback I2CP endpoint; do not assume retained Router
class availability proves safe startup. Current retained modified JAR contains
Router and Streaming classes, but no native router executable was found on PATH.
No such backend was started here. HTTP trackers and BT peers still require real
Streaming protocol6 lifecycle and the original policy/owner integration; SAM
equivalence, public/default activation, general tracker URLs/announces/peers,
retries, router auth/TLS, and full P07 parity remain open.
