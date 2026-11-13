# Shared Public ratings for existing downloads

Native implementation `3a0285ea029d45fa6b455192d8b2c81f3923bd14`, tree
`f1eecfb274b6eb50c481f3d0143053ad76f257fe`. Original azrating
`1629340340d0bc391d9e582f9ee989a93d550581`; BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Prerequisites:
`7733b15b84cb00f2f1ca9e115ad3d3bbf69eef8a` and
`ff01ff720889a2c9a263acf5ccb865f015d430fa` (see chapter task).

Existing downloads now use the single shared Public UDP/DHT owner for ratings
on IPv4 and IPv6, independent of RCM preferences. The original application key,
SHA1 DHT target, bare rating bencoding, anonymous single-value flag0x10 and
509-byte DDB publication limit are retained. Original model decoding, binary32
averaging, partial-add behavior and originator-ID-plus-value deduplication are
reused. Deduplication stays per IP-family operation; equal independent ratings
are not collapsed across the two networks. Local fallback is used only when no
value event was accepted; malformed nonempty replies do not create a fallback.

The qualified publication policy now drives real requests/completions/timers.
Read scheduling uses original 1/2/4/8-hour periods, incomplete-first cold choices,
priority requests and20-second read completion delays. Publication completions
use the original10-second redispatch receipts. Native deterministic tie order is
not claimed identical to Java object-key HashMap iteration. All-library cached
hints avoid eager metadata scans and are not authority: current membership,
full identity, local contribution, plugin/DHT switches and Public bind/network
permissions are checked before effects and response acceptance. Existing Public
ratings of private-tracker downloads follow the original rating plugin policy;
they do not inherit RCM's private-content exclusion. Tracker URLs/credentials
never enter rating values. Revoked sources withdraw local serving and matching
presentations; cancel/close invalidate pending work. Expired network actions
allow final fresh-source timeout handling rather than masquerading as changed
configuration. An empty router does not repeatedly trigger profile preflights.

The host registers independent library hints and queues changed saved ratings.
The original RatingColumn ID, saved global/personal sorting preference, numeric
sort and tooltip are projected into both classic tables. Existing33-column
native preferences upgrade by appending a hidden rating column while preserving
all earlier choices; original table layouts remain compatible. The editor shows
current remote averages/comments without changing an unsaved local draft. The
original globalRating attribute is updated through the existing exact state/
cache writer CAS, merging current unknown fields and resume/counters. Java can
read the round-trip float text; byte-identical Float.toString spelling is not
claimed. No queue state, resume pieces or torrent payload is changed by ratings.
The [editor image](rating-editor.png) contains generated data only.

Validation: original publication corpus653 eligible observations passes, with
193 excluded source-only fault observations still excluded. Publication bounds,
CLI bounds and read scheduling tests pass. Editor19 Qt rows, host wiring and
both-pane/all-column sorting19 rows pass, including numeric Rating sorting and
binary32 display truncation. Existing preference codec plus33-to34 upgrade pass.
New shared rating tests pass10 Qt rows in24 seconds, including automatic queue/
readback, late replies after revocation, oversize local retention, original
fallback rules and owned IPv4/IPv6 exchanges. Actual pinned Java UDP rating
exchanges pass4 rows in5.3 seconds; four remaining search/privacy scenarios pass
6 rows in17.8 seconds. Prior owned/Java association and search data rows also
passed in the affected aggregate attempt. That earlier aggregate reached its
old120-second bound after a flaky JavaIPv4 row; it is not reported as a passing
aggregate. The loopback fixtures now reserve distinct original NodeCache ID
buckets (same-IP v4 ports are grouped modulo5 and v6 modulo8), and final Qt test
metadata explicitly includes the added late/oversize slots. Only affected
scenarios were repeated after correction. Static desktop host compiled with
GCC16/C++23/Qt6.12; no new sanitizer/platform/release-package claim is made.

This completes the bounded owned-download shared-owner chapter, not all F12
rating/plugin parity. Generic hash-only/RCM ratings, original automatic chat
notices, DDB diversification/packed multi-value handling, I2P ratings and full
graphic/header-control parity remain in the extensions task. No daily-use
executable, live profile, running application, SDK or preview manifest changed.
