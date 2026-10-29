# I2CP framing, signed registration and initial channel

State: in_progress. Feature F06.i2cp_initial_session. Source e124abb90ab846c4b057a6b868004c0acb737118 extends the original pure framing/no-auth channel with exact SessionConfig/Create/Reconfigure, DSA0/Ed7 signatures and Java UTF16-sorted UTF8 options. Current CREATED stores a genuine ID; matching UPDATED commits original putAll merge. Identity is borrowed synchronously for signing; no private key persistence. Explicit numeric-loopback only, bounded request/write/read, deadlines and generation/revision guards remain.

Current focused7-suite three-lane/private and actual two-artifact Java object proof: ../evidence/20261029-signed-session-local-publish/verification.json. Failed helper-only attempts and their precise corrections remain in that evidence; no native/whole-suite replay for helper fixes. Older evidence remains historical.

Original i2p-java9134f808337b401e8e53c73734c81fab04280c9d; modified plugin33d97eca67a9f332e3d4da7e7492ec54f4bec078 context remains separate. No whole modified-core source equivalence.

CREATED is registration only, not original OPEN. LeaseSet2 genuine independent encryption key/signature and renewal handling is the next prerequisite. No send-after-CREATED shortcut. Auth/TLS/remote/offline/unsupported keytypes, automatic Clock/profile/router selection, public/backend activation, full payload/lookup/tunnel lifecycle and performance remain open.


## Owned LeaseSet2 prerequisite

Source 6726a4cf7b017ac31d6aeede15f339c3ec2b71f7 adds explicit consuming createLeaseSession and retained-signer reconfigureLeaseSession. Real RequestVariableLeaseSet37 creates signed standard LS2/type3/Create41 with independent OpenSSL-generated Enc0 ElGamal pair.1..16 actual leases, original publication/expiry encoding, current generation/ID/acknowledged policy, renewals, deadlines and secret cleanup are covered. New actual both-JAR objects4cases/reference/direction; all7 affected suites passed each lane/private, full product buildGCC only. See ../evidence/20261029-normal-dht-leaseset2/verification.json. LeaseSetSubmitted is sign+socket-enqueue, no router ACK/OPEN/delivery. Other keys/LSvariants, auth/TLS/streaming/receive/public/default activation remain open.


## Lease-backed outgoing datagrams

Source `d384a850980f1b6245230ea852d617b11c405e11` adds explicit Datagram2/claimed3 Send5/Expires36 with current signer/recipient and genuine unexpired LS2 socket enqueue required. CREATED alone remains insufficient. Fixed-header bounded gzip, one pending tracked nonce, immutable initial reliability mode, monotone nonreused nonce, matching ID/status, independent absolute deadline and local-failure certainty are covered. Fresh affected7/5/5 and private7 passed; both actual Java artifacts exchanged8send+4status+1payload cases/reference/direction. Payload31 DTO parsing is not a receive listener. Receive, routing/tunnel/backend/streaming and public/default activation remain open. [Review](../evidence/20261029-saved-ratings-i2cp-datagrams/REVIEW.md).


## Bounded explicit datagram receiving

Source `9fca696181291fd9a1dc7d62942c76d23afbba11` qualifies legacy Available→Begin6→Payload31→End7 and immutable initial fastReceive directPayload/noEnd. Queue4/160KiB, one legacy fetch, control4/recent64, unread-ID protection beyond recent rotation, signed2/current recipient versus unsigned claimed3, corruption drops and queued reentry/delete/close fences are covered. End releases router storage, not application acceptance; finite recent history is not lifetime replay protection. Fresh5/5/5 and private5 passed, with four actual receive/control Java cases per reference/direction. R1 Clang fixture format warning was fixed by one newline; all raw attempts remain separate. [Review](../evidence/20261029-i2cp-bounded-receiver/REVIEW.md). Actual stalled-socket overflow, real router/lookup/Streaming/backend/public activation remain open.

## 2026-10-29 executor and explicit tracker qualification

Source `f9c72f14be1494b6d75b38288f9eff256a1a878b` qualifies bounded profile I/O off the DHT network owner and explicit I2CP hash lookup/connect/scrape. Fresh GCC17/Clang12/leak-enabled ASan+UBSan12 suites, private-runtime17 and actual official/modified Java2signing families/10frames per reference/direction pass. [Review](evidence/20261029-profile-io-i2cp-tracker/REVIEW.md).

This closes the bounded execution slice only. Public maintenance/router/Streaming/default activation and full performance/parity remain open; consult the linked review for exact limits.

## 2026-10-29 actual isolated-router acceptance

Actual isolated-router acceptance on native `f9c72f14be1494b6d75b38288f9eff256a1a878b` passes warmed Hash38/39 and signed2connect/claimed3scrape/RAW18 through a real Java26 router and external tracker. All3subjects exit0, both clients retire, fences hold and dist remains `0cb123398f3a5462ef6ef1e444f245dd8ce09c757bdc804213fa2f82a50ec976`. Warmed actual signed-LS2 cache is an explicit fixture prerequisite; discovery/tunnels/Streaming/default activation remain open. [Review](evidence/20261029-i2cp-isolated-router/REVIEW.md).

## 2026-10-29 owned routing and original I2P Streaming

Source `514a238f0efa749e7a7fb1e68f8123af78e766da` qualifies opt-in owned Azureus self-lookup/contact discovery/cached forwarding and bounded outbound I2P Streaming. Fresh GCC14/Clang11/leak-enabled ASan+UBSan11 default suites, separate original Java manager acceptance on Clang/SAN, private14+GCC actor1 and actual isolated-router HTTP transfer pass. [Review](evidence/20261029-routing-streaming/REVIEW.md).

This closes the bounded interoperability slice. Current one dist replaces its predecessor; all existing component states/baseline fields and22dependencies remain unchanged. Public maintenance/backend/default activation and complete P05–P13 remain open.

## 2026-10-29 owned maintenance, contacts and Enc4

Source `0f7e9216253a7881fae11e1a7dae8a849cb333e8` qualifies opt-in owned Azureus timed maintenance/contact persistence and explicit Enc4/X25519 LeaseSet2 keys. Actual GCC/Clang/leak-enabled ASan+UBSan suites 16/13/13, private 16, both-JAR original objects (4 cases per reference and direction), and real isolated-router Enc4 tracker exchange pass. [Review](evidence/20261029-contacts-enc4/REVIEW.md).

One checked dist replaces its predecessor after both registry validations. Existing 20 upstreams, 108 components, 22 dependencies, states and baseline/null integration fields are unchanged. The explicit AllowPeer-to-normal-routing contact attachment seam, public/default/Host routing and I2P backend activation, signed-negative network paths, multitype lease keys, remote ECIES ratchet/multihop discovery, native Streaming window/performance and the broader P05–P13 remain open.

## 2026-10-29 PublicBound and bounded Streaming window

Source `04b6287a38cec6bd123e2648ada278ea348b85db` closes the resumed same-source current cohort: Clang passed its original complete16-suite invocation; GCC18 and SAN15 unaffected actual rows are retained from their original failed invocations, with only each pre-subject authorization-blocked Public suite rerun. Original failures remain linked; no full fresh rerun is claimed. This qualifies the explicit bounded native Streaming window: the associated actual GCC/Clang/leak-enabled ASan+UBSan result covers 19/16/16 selected suites, including all five retained modified-manager controls and cap2/cap8 128KiB upload/digest/FIN/EOF cases. 19 private commands use 18 exact GCC test images against private Qt. PublicBound executes through the exact copied enforcing network/user-namespace wrapper. [Review](evidence/20261029-public-bound-streaming-window/REVIEW.md).

Actual controlled-route private observations (same40ms actual peer-frame delay): upload-cap2: 57471 body bytes/s, 2280637us, peak flight 2, retries 0, upload-cap8: 152335 body bytes/s, 860415us, peak flight 8, retries 0. These are finite fixture measurements, not a guaranteed speedup or production performance-parity claim.

Multi-stream/listener/backend integration, public/default/Host activation, anonymous remote tunnels and broader performance/resource parity remain open. No old Java-object/router/provider pass was pooled into this window result.
