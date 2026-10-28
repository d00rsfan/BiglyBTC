# Independent review: pinned LT final peer-counter fence

**Conditional source proof is defensible for exactly four counters. Runtime
certification remains pending implementation review and real pending-callback
qualification.** This is not a portable libtorrent API guarantee, an exhaustive
socket/wire count, storage durability, or lossless accounting across crash/kill.
No build, runtime, Java, network, profile, Git command or subject edit was performed
for this review. The mutable implementation overlay was not reviewed as frozen.

Reviewed the complete DESIGN/CONTRACT/proposed API/protocol and the counter,
callback, torrent/storage lifetime and stats-response paths below. Exact context
and all 36 selected LT source files are recorded in `review-fence-oracle-pins.json`
SHA `d86bb5910017840853e3c2779cfac50e472c944c87cb0aeaa222f509ef1f6820`.
All selected files match the actual release archive bytes. All 444 relevant source
hashes match the previously accepted source map; all 17 proposed native context
pins match their current files. The correct release source root is
`/tmp/biglybtc-deps/libtorrent-2.0.12-release/libtorrent-2.0.12`.
Archive SHA is `fa0168f5c2b510105ce4e215f5863c80597112b99fc4f30514de2a6ea84da1b5`.
Recorded upstream is `740a0b9aeabe00e762cc0efe4a0f27593db2550b`; this review
does not manufacture a new Git provenance proof from that release folder.

## Why the four values can stop changing

Searching the source/header tree for the four counter enums yields their only
session counter mutations in `src/session_impl.cpp:3322–3343`:

| Named metric | Exact increment |
|---|---|
| net.sent_bytes | bytes_payload + bytes_protocol |
| net.sent_payload_bytes | bytes_payload |
| net.recv_bytes | bytes_payload + bytes_protocol |
| net.recv_payload_bytes | bytes_payload |

`src/torrent.cpp:10600–10610` forwards sent/received values to those two session
methods. Its only forwarding callers found are `peer_connection::sent_bytes` and
`received_bytes` at `src/peer_connection.cpp:1142–1170`. Each checks
`m_ignore_stats`, locks its weak `m_torrent` into a **local strong reference**, and
returns without forwarding when that lock fails. The sent helper's optional
extension notification precedes that lock, so arbitrary/new extensions must not
be added to the reviewed closed service domain. The inspected tracker counters
(`tracker_manager.cpp:214–225`), IP-overhead paths, disk counters and DHT stats
updates are different fields; they are not extra writers of these four metrics.

`torrent_handle.hpp:1424–1427` stores a weak pointer;
`torrent_handle.cpp:763–766` implements `is_valid()` as `!m_torrent.expired()`.
The same retained weak control block cannot become live again. Handle equality
uses weak ownership identity (`torrent_handle.hpp:1374`), remaining useful after
expiration and distinguishing a later same-hash/control-block admission. Do not
replace this check with hash equality, `in_session()`, peer count or two equal
counter samples. Public docs explicitly caution against general use of
`is_valid()`, and warn that removal alerts precede some remaining tasks.

Thus, if **every traffic-capable admitted torrent is registered**, every retained
handle refers to its original control block, all those handles are observed
expired, and no new/unregistered torrent may be admitted, the inspected forwarding
path cannot perform a later increment. A callback already holding a strong
torrent keeps the handle valid until it releases that ownership. A later callback
holding only a peer and weak torrent cannot reconstitute it after expiration.
The torrent destructor does not directly forward sent/received bytes. This is
the source proof; a counter coincidence is not its replacement.

## Actual callbacks and the storage owner chain

Socket read/write handlers (`peer_connection.cpp:5932–5943,6026–6037`) use
`aux::handler` containing `shared_ptr<peer_connection>`
(`aux_/allocating_handler.hpp:324–355`), not an unconditional strong torrent.
Read completion calls the BT parser, whose receive/error paths account through
the weak-lock helper; successful send completion calls `bt_peer_connection::on_sent`
and then that same weak-lock sent path. Pending socket handlers/“undead” peers may
outlive torrent removal. Session close keeps such peers until references drain
(`session_impl.cpp:3205–3228,3395–3409`). They can still update other diagnostics;
they cannot forward the four named counters after all the relevant weak locks
have become permanently unavailable.

The storage chain makes expiration stronger than just an empty session map:

1. `torrent.cpp:1822` passes `shared_from_this()` as the new storage's owner.
2. `mmap_disk_io.cpp:408–421` calls `storage->set_owner(owner)`.
3. **`mmap_storage.hpp:142,160` retains that owner in a strong
   `shared_ptr<void> m_torrent`**, explicitly to keep torrent-owned file metadata
   alive until storage destruction. This additional header must remain a pinned
   proof input; checking `mmap_disk_io.cpp` alone misses the decisive ownership.
4. Disk jobs own strong `mmap_storage` (`aux_/mmap_disk_job.hpp:128`); completed
   jobs and callback captures remain alive through `call_job_handlers` and job
   destruction (`mmap_disk_io.cpp:1789–1825`). Download write completion also
   explicitly captures a strong torrent (`peer_connection.cpp:2987–2989`).
5. Deferred periodic ticks keep weak storage references, but a tick already
   promoted to strong storage also retains that strong torrent owner
   (`mmap_disk_io.cpp:704–706,1568–1578`).

Accordingly, those storage/job/promoted-tick references cannot survive while the
same torrent has truly expired. Storage destructor work, including partfile
destructor work, precedes release of its `m_torrent` member. **This corrects my
provisional message suggesting that such independent queued storage references
could run partfile destruction after torrent expiration. That inference was
wrong; it omitted the strong owner field.**

Removal itself is weaker. `session_impl::remove_torrent` removes the map entry and
calls abort (`5284–5326`). `torrent::abort` disconnects peers and posts a disk stop
job bound to its strong self (`torrent.cpp:4928–5010`). Mmap stop is a fence job
and releases files (`mmap_disk_io.cpp:980–990,1268–1274`);
`torrent::on_torrent_aborted` drops `storage_holder` and posts the removal receipt
(`9596–9607`). Required matching removal receipt **and** subsequent expiration are
both useful; removal alone does not establish this chain has drained. The abort
exception branch resets storage and may emit cache-flushed without a removal
receipt; a required missing receipt must therefore remain a failed/uncertified
retirement, never an inferred success.

## Fresh response ordering is mandatory

`session_handle::post_session_stats` (`session_handle.cpp:229–232`) dispatches
the handler to the live LT context through `async_call` (`92–115`). The normal
owned session starts one network event-loop thread (`session.cpp:340–347`);
the reviewed engine must retain that arrangement, not introduce another runner
of that context or direct cross-thread internal counter mutation.
`session_impl::post_session_stats` (`4810–4835`) emits a copied counter alert;
`session_stats_alert` copies the array at construction (`alert.cpp:2261–2276`).
The four fields are not disk/DHT gauge updates. After the no-live-torrent proof,
their values are stable, so the alert copy establishes the desired exact values
without a race among those four reads.

There is no request ID on this alert. The source proof requires one sole alert
consumer and at most one outstanding stats request, including inherited operation
samplers. Before posting the seal request, positively consume/resolve the older
request's response. A timeout must not just clear an outstanding flag and allow
an old response to satisfy a new request. If old-response provenance is lost,
return Uncertified. A pair of equal responses cannot identify which request
produced either one. Stop coalesced idle submissions/admissions during this gate.

After old response consumption **and** weak expiration, post the fresh request,
copy all four primitive fields before any next alert pop, validate runtime mapping
and bounds, and construct the fixed receipt before optional callbacks. Counter
span storage belongs to the alert allocator; `alert_manager::get_all` rotates and
resets allocator generations (`alert_manager.cpp:109–132`). Do not retain spans.
Critical stats/removal alerts can still be dropped on capacity/allocation failure
(`aux_/alert_manager.hpp:72–99`); dropped-alert indications, invalid/incomplete
mapping, absent response or the five-second certification deadline must produce
an explicit uncertified tail. The deadline does not bound an underlying join.

## Necessary interpretation limits

- A successful retirement between operations certifies a **settled checkpoint up
  to that admission cut**. A subsequent torrent in the same engine epoch can
  legitimately increase its counters. Only permanently closing admissions makes
  it an engine-epoch Final through teardown. The private `FinalPeerCounters` name
  must not lead consumers to advertise an intermediate checkpoint as forever-final
  application totals.
- These are exactly LT's four named peer counters. For example,
  `peer_connection::on_send_data` returns on error/disconnecting before its normal
  `on_sent` accounting (`6435–6526`); later weak-lock failures can suppress forwarding
  too. The seal does not repair LT's treatment of canceled/partial callbacks,
  count kernel buffers, or imply all network bytes were exhaustively observed.
- Weak expiration is not an error-free storage/durability receipt.
  `mmap_storage::release_files(storage_error&)` ignores its error parameter and
  ignores `part_file::flush_metadata` errors (`mmap_storage.cpp:482–495`). Storage
  and partfile destructors also ignore flush results (`129–137` and
  `part_file.cpp:147–152,404–455`). The strong owner chain settles their lifetime,
  not their successful I/O outcome. Pool eviction may already be closing a mapping
  elsewhere; public `async_release_files` itself only promises a point of closure,
  not a permanent all-descriptors-closed state (`disk_interface.hpp:246–254`).
  Preserve the existing checked payload fd fsync/identity/publication prerequisites;
  do not let `storageReleased` silently mean successful hidden metadata flush,
  WriteQuiescent, ClosedFiles or Durable. Any stronger claim needs separate proof.
- `session::abort()` returns a join proxy and disables notifications; destruction
  invokes actual abort/join paths (`session.cpp:486–535`). Certify while the
  session is live, then destroy/join on its worker with lease held. Do not query a
  destroyed session, restart its context or inspect counters after join.

## Required qualification recommendations

Before advertising certification, add source-bound tests with recorded causal
evidence, not just transfer success or matching samples:

1. Owned loopback upload and download with real socket completions pending at
   removal. A public peer-state sample alone can race before removal; either
   instrument the exact actual callback boundaries with a separately reviewed
   test-only observer, or report the overlap test as inconclusive if it cannot
   prove those callbacks were pending. Keep release binary versus instrumented
   test-library evidence distinct. Exercise positive-byte canceled/error callbacks.
2. Hold a real generated-payload disk operation/callback via a target-matched
   observer/wrapper. While storage/promoted tick or a captured strong torrent is
   held, assert no weak-expiration receipt can be minted. Release it, observe the
   actual matching remove/expiration sequence, and only then the fresh stats copy.
3. Keep a peer-only socket callback alive after torrent expiration; show later
   callback processing/diagnostics without a four-counter change. A subsequent
   live fresh response may confirm the source result but is not its sole oracle.
4. Start an ordinary stats request before retirement, delay its delivery, and
   prove it cannot seal. Consume it before a separately recorded fresh request.
   Lost/dropped old response, required removal loss, invalid metric mapping and
   deadline must fail certification without fabricating zero or losing the primary
   transfer failure. Fake queue tests supplement, not replace, actual callback tests.
5. Repeated same-hash admissions in one persistent owner: old/new handles differ
   in ownership identity; stale resume/removal/errors cannot complete the new
   operation. Verify retained absolute totals, baseline-relative operation reports,
   optional observer exceptions and close with no active torrent.
6. Preserve success/cancel/error/finalizer and lease/destruction order. Cleanup
   ignores the user's stopped operation token. Five seconds bounds certification
   attempts only; forced reset/blocked join/no actual response remain uncertified.

Principal exact pins (all additional full hashes are in the JSON input map):

| File | SHA-256 |
|---|---|
| DESIGN.md | `e7e1fab29fde5a003a7a44100600a0e54a34770a2e8666c92dd2bf37800a4ac7` |
| CONTRACT.md | `5ded928a630e4475354c6e14843c37ed7c66a40a445d2cd036b9d987de3977fc` |
| LT src/session_impl.cpp | `e33778939a03634f7ca588752f924afcdf790ac48ddd2a127af71e00f34d1c65` |
| LT src/torrent.cpp | `d0b8d64c54583cb9287d20aaf5ea5bc2e85cbf0caa5d5c9df42f40c77187d185` |
| LT src/peer_connection.cpp | `41d22135c8f01adb1eaf6f668b7113d09b270da0d593b2b9cb6fb866048ee7e4` |
| LT include/libtorrent/mmap_storage.hpp | `8b7d60a7e946eaa789d31fcd0c2f720a6bda1fbaa0a345218919fdedad44ee87` |
| LT src/mmap_disk_io.cpp | `e28fdbb4f9eec4f7808098051987a5f9beab1fca1be3e5d0b628fb73c4ae1410` |
| LT src/mmap_storage.cpp | `efd0a58f4d1ae340c7b2b3c66f3c78b9807028b773f195b6bd27befc68a25dc4` |
| LT src/alert.cpp | `750ccc0d3a62ffd6e2dbce1d9930556bc9613ec3b10a40825bb28bc863179773` |
