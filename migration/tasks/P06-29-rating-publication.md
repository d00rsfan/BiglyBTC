# P06-29 Ratings publication queue

Status: implemented bounded publication policy; three fresh compiler/sanitizer lanes pass.
Evidence: migration/evidence/P06-29-rating-publication/{matrix.json,review.md}.
Component `plugins.azrating_publication_queue`, feature `F12.rating_publication_queue`.
Plugin baseline: `1629340340d0bc391d9e582f9ee989a93d550581`;
core context: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The exact original RatingsUpdater.addForPublish and downloadRemoved bodies,
including three nested callbacks, are pinned against eleven source files.
A manual dispatcher FIFO, recorded timer deliveries, scripted RNG/clock and
write callback expose source policy without starting threads, DDB, a real
plugin or ordinary core. Critical-region annotations describe the source seam;
they do not prove a native mutex or scheduler equivalent.

The independently frozen corpus has 846 observations across 56 scenarios:
362 queue/dispatch, 177 completion/timer, 114 removal/stop, 99 boundary-fault,
and 94 callback-contract. The first 653 are eligible for a later explicitly
bounded native policy projection. The remaining 193 receive zero native
source equality credit. Six outward command failures and three caught runnable
failures have strict predeclared contracts. Generated expected bytes stay fixed.

The first 845-command plan exposed an invalid helper schedule: a queued runnable
was deliberately skipped while backlog remained, so DONE 2 referred to no write.
The failed attempt, partial outputs and original plan remain preserved. V2 adds
one STEP before DONE 2, leaving the source harness, error validator, all eighteen
helper classes and successful preceding groups unchanged. The fresh V2 helper
froze all 846 observations. No accidental IndexOutOfBoundsException was added to
an exception allowlist. See source-freeze-verification.json and repair-review.

Preserve random insertion's exclusion of the tail, duplicate removal before RNG,
every-add dispatch, backlog-before-inflight checks, synchronous write completion,
10,000ms wrapped timer deadlines and redispatch of the same captured runnable.
A new enqueue may start work before an old timer fires: there is no global
cooldown. Removal clears queue/cache/lookup intentions but does not cancel
inflight work or pending runnables/timers. Source destroyed checks occur at
runnable entry. Old repeated callbacks are characterized separately; native
completion handles must be owned, once-only and generation-bound so they cannot
clear a later write. A real native Close invalidates outstanding handles and
must remain available at capacity, distinct from the source destroyed flag.

Native draft: build/recovery/active-drafts/rating-publication-native.
Target: native/services/rating_publication. Use a single serialized owner,
owned bounded input tapes and receipts, no retained provider callbacks, private
state with weak generation handles, explicit capacity preflight and no cycles.
Independently review, then qualify GCC16, Clang24 and ASan/UBSan/LSan including
all 653 eligible projections plus lifetime, stale/duplicate handle, capacity,
move, shutdown and CLI boundaries. Source fault/callback groups remain preserved.
Actual AsyncDispatcher/Timer integration, attribute storage, DDB publication,
download eligibility, plugin host activation and editor UI remain later work.

Evidence: migration/evidence/P06-29-rating-publication;
source manifest: migration/fixtures/rating-publication/manifest.json
(SHA256 88c89f6c60f8488cc6164da13d477a560784c99232a5abdc1afda6d91fe4c162).
No live profile, torrent data, running original process, DNS or public network
is used. Null port/integration hashes remain until the covered scope qualifies.

Qualified per compiler:653 eligible source comparisons,386 native assertions,112 CLI observations/14 cases; three CTest. Original193 boundary/callback rows remain source-only.100 source inputs guarded;13 historical ignored inputs map to exact source archives. Actual DDB/timer/host integration remains pending.

Native implementation: `d4d539e761e136c9916a9305c2f8fff48e5061f8`;100 source inputs verified as actual Git blobs, including13 explicit historical archive mappings.
