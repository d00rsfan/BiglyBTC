# General Public chat foundation

Status: implemented and tested foundation; the general-room chapter remains in progress.
Original core baseline: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
Original azmsgsync baseline: 4209aa5029e3035cbb09dc3a83e9491d3f36fbc6.

The resumed drafts now implement the original DATA transfer format and a bounded
actor with multiple handlers, five concurrent calls per handler, fragmentation,
retry, cancellation and lifetime-bound replies. The timeout is checked before
clock arithmetic; responses keep the original absolute budget. A lost small
reply is resent without delivering its request to the application again.
Unregistering a handler cancels its operations and invalidates old receipts.

The protocol core implements encrypted general-room synchronization, original
bloom filters, signed messages, replay/duplicate control, bounded history and
Buddy payload fields. The shared plugin crypto now supplies reusable signing
identities and compatible new-key generation. A generated 512-identity room
exercise retains the original 128-message window. Catch-up repeats periodic
exchanges to account for probabilistic bloom false positives; more=0 alone is
not a complete-history certificate.

The profile adapter retains the same held profile lease, original room identities,
favourites/options and history names/formats. It preserves root extensions and
extensions on retained history nodes/messages. Invalid existing keys are refused
without rotation. History publication checks its preimage, retains the previous
file and refuses symlinks and unfinished writes. All profile checks used generated
profiles.

The independent Qt page supports room selection, participants, a plain-text
transcript and composer, drafts, favourite/history intentions and send
acknowledgement. Snapshot updates do not post messages or rewrite options.
Untrusted message text is rendered literally. The page still needs its application
service/menu binding.

Validation used GCC16 Release, Qt6.12.0, OpenSSL3.5.5 and JDK17. Source-derived
original message helpers generated/verified messages, signatures, requests,
replies, blooms and ordering in both directions. Actual original
DHTTransferHandler callers and receivers passed four owned-loopback DATA variants;
the complete DATA executable passed 18 Qt rows, including native concurrency,
loss, cancellation, wrong-key/network and close cases. The profile executable,
512-identity room exercise and two Qt page scenarios passed. Full logs and generated
reference classes stay in the ignored build workspace.

Public bootstrap, room-key discovery/publication, shared UDP-port integration,
network-policy scoping, the independently scheduled application room owner and
live room acceptance remain. Real room data and real-room posting were not used.
The implementation is not yet an operational Public-chat release. Current repair
work prioritises the reported existing-library resume/Java handoff regression.
