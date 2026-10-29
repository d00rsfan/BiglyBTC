# I2CP framing, signed registration and initial channel

State: in_progress. Feature F06.i2cp_initial_session. Source e124abb90ab846c4b057a6b868004c0acb737118 extends the original pure framing/no-auth channel with exact SessionConfig/Create/Reconfigure, DSA0/Ed7 signatures and Java UTF16-sorted UTF8 options. Current CREATED stores a genuine ID; matching UPDATED commits original putAll merge. Identity is borrowed synchronously for signing; no private key persistence. Explicit numeric-loopback only, bounded request/write/read, deadlines and generation/revision guards remain.

Current focused7-suite three-lane/private and actual two-artifact Java object proof: ../evidence/20261029-signed-session-local-publish/verification.json. Failed helper-only attempts and their precise corrections remain in that evidence; no native/whole-suite replay for helper fixes. Older evidence remains historical.

Original i2p-java9134f808337b401e8e53c73734c81fab04280c9d; modified plugin33d97eca67a9f332e3d4da7e7492ec54f4bec078 context remains separate. No whole modified-core source equivalence.

CREATED is registration only, not original OPEN. LeaseSet2 genuine independent encryption key/signature and renewal handling is the next prerequisite. No send-after-CREATED shortcut. Auth/TLS/remote/offline/unsupported keytypes, automatic Clock/profile/router selection, public/backend activation, full payload/lookup/tunnel lifecycle and performance remain open.


## Owned LeaseSet2 prerequisite

Source 6726a4cf7b017ac31d6aeede15f339c3ec2b71f7 adds explicit consuming createLeaseSession and retained-signer reconfigureLeaseSession. Real RequestVariableLeaseSet37 creates signed standard LS2/type3/Create41 with independent OpenSSL-generated Enc0 ElGamal pair.1..16 actual leases, original publication/expiry encoding, current generation/ID/acknowledged policy, renewals, deadlines and secret cleanup are covered. New actual both-JAR objects4cases/reference/direction; all7 affected suites passed each lane/private, full product buildGCC only. See ../evidence/20261029-normal-dht-leaseset2/verification.json. LeaseSetSubmitted is sign+socket-enqueue, no router ACK/OPEN/delivery. Other keys/LSvariants, auth/TLS/streaming/receive/public/default activation remain open.


## Lease-backed outgoing datagrams

Source `d384a850980f1b6245230ea852d617b11c405e11` adds explicit Datagram2/claimed3 Send5/Expires36 with current signer/recipient and genuine unexpired LS2 socket enqueue required. CREATED alone remains insufficient. Fixed-header bounded gzip, one pending tracked nonce, immutable initial reliability mode, monotone nonreused nonce, matching ID/status, independent absolute deadline and local-failure certainty are covered. Fresh affected7/5/5 and private7 passed; both actual Java artifacts exchanged8send+4status+1payload cases/reference/direction. Payload31 DTO parsing is not a receive listener. Receive, routing/tunnel/backend/streaming and public/default activation remain open. [Review](../evidence/20261029-saved-ratings-i2cp-datagrams/REVIEW.md).
