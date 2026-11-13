# Large-library shutdown progress and checkpoint allowance

Native `f0599e687ba9b1b8c4db9d0cafe6ce473cb1acba`, tree `1c9d259a4b8f897104135527b17c90bb0cea88fa`, repairs the observed shutdown presentation and overly small size-derived
checkpoint allowance. Original reference cb8c6cb9bb97916cbc7e80338a017200ffc3e71f:
core/src/com/biglybt/core/global/impl/GlobalManagerImpl.java stop/save lifecycle.
The exact source-qualified SDK is unchanged.

## Cause and behaviour

Normal close retires small groups to keep essential SDK pause/flush/removal
receipts bounded. Each owner still commits its torrent state and the full saved
inventory. A sufficiently large library could exhaust the old allowance while
performing synchronous persistence. The main Qt event loop remained alive but
normal actions were disabled without a useful closing progress display. Other
unretired owners could still emit network diagnostics. On deadline failure the
SDK joins and the existing warning reports uncertified progress; conservative
piece maps may then need original-client checking. The warning was not itself
a deadlock. Private service metadata was captured after confirmed Exit and
kept separate from the next fresh pre-launch observation baseline.

Close now reports actual stopped/remaining owner counts, at most once a second,
and a GUI-owned heartbeat keeps the waiting time visible even during worker I/O.
The allowance changes from 10+owners/10 to 10+owners/2 seconds, capped at 1000.
All payload fsyncs, metadata CAS/durability order, flush/removal/weak-expiry/join
proofs and final counter qualification remain intact. Observer exceptions are
ignored/disabled; reentry is refused. No stale piece map is restored and no
unconfirmed completion is relabelled as successful.

## Qualification

A generated 512-torrent normal Host ran on a persistent local filesystem, with
two sparse pieces and 2 KiB opaque inventory bytes per row. Actual close took
31784 ms, with 31 GUI heartbeat messages and monotone retirement progress through 512.
It joined, released storage, certified final accounting and preserved all piece
maps before and after close. DHT/trackers/Internet were disabled; only generated
profiles/data and owned loopback endpoints were used. Its temporary root was
automatically cleaned. Earlier tmpfs measurements did not cover this fsync cost.

Seven actual bridge control/early-close/Force Start QTests passed. The focused
managed group retained 19 warm/hybrid/negative cases and exposed a new observer
reentry assertion at the final callback. That callback was moved inside the
active-call guard; the final targeted four QTests passed, including a throwing
observer, reentry rejection, monotone progress and partial-map preservation.
The I/O/finality path did not change in that last observation-only correction,
so the already green persistent run was retained rather than repeated.

No real profile was read while the user ran Java and no user payload was opened
by development tools. Actual metadata/logs/identities and hardware details stay
outside public Git. A late userspace perf attempt produced no CPU samples and
is not used to claim a hot-function diagnosis. No sanitizer matrix or separate
human/sub-agent review is claimed.

## Limits and handoff

This does not batch inventory transactions or establish Java performance parity.
The effort deadline cannot safely interrupt a blocking kernel fsync or mandatory
SDK join. Real-library timing and clean post-Exit acceptance require a later
user observation. Preserve the failed-exit private snapshot; capture a fresh
baseline only after the intervening Java run exits normally. Prepare the same
preview entry with this update, then STOP. New distributed search stays deferred.


## Delivered replacement and STOP

Updated frozen image SHA256 `9e3e6d57c1b7aafdb8d02135d375d92bf4aa35ace7688e4ad7eba95ad4643e3d`,
19816912 bytes, passed launch/runtime input validation. The same chat-preview entry
(and daily-use forwarding entry) is retained. No application was started during
delivery and the user's live Java profile was not accessed. The exact obsolete
generated image was removed after its lifetime flock was exclusive; no rollback
copy was kept. Failed-exit diagnostics remain private. A fresh compact baseline
will be captured after Java's normal Exit, before the next Native launch.

STOP here. No keyword search or another chapter is active. Real-library closing
time and clean round-trip acceptance remain for the user's next observation.
