# Public keyword search on the shared owner

Native `cbf37c29f035b840748d6c03665dfe7d94bb843a`, tree `9664f4ac215a7119a7dcf6f202620982b699b590`;
original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Builds on the separately
recorded [query/Bloom/DDB Read foundation](../20261113-related-search-foundation/REVIEW.md).

The existing shared UDP owner now serves original RCM query, Bloom fetch and
update messages and provides cancellable keyword search. It uses cached and
current nonlocal DHT associations, original live-router closest-contact Bloom
scope, three concurrent queries, 50 attempted contacts and a 25-second search
budget. Results are bounded to 500 transient rows; they do not silently change
the saved discoveries cache, torrent queue, resume maps or payload. Incoming
responses retain original caps of 30 hits, 50 numeric hints and 16 KiB. The
normal Java AddOnly Bloom map is reused, with separate chat/search quotas.
Harvested filters cap at 50, update after 15 minutes and age out after an hour;
failed-contact history caps at 128. Incoming per-IP limits use bounded exact
counters (10 queries/minute and one Bloom operation/five minutes), rather than
porting the original probabilistic requester counters and their false positives.

Fresh original settings, Public bind policy and source gates are checked before
network effects and response acceptance. Local/private/non-Public/disabled
owned sources are filtered before forming public search results or Bloom words.
The read-only DHT view excludes local records, replaced records and deletion
values, has no read/store counter side effects and caps copies at 8,192 values /
4 MiB. Tracker URLs and passkeys never enter the result wire format. Malformed
initial read ranges are rejected before allocating request slots. Cancellation,
close, setting changes, late responses and expired operations retain the actor's
existing generation, receipt and source checks. Public+I2P settings are retained;
this chapter grants no I2P transport or unresolved-contact adapter permission.

The classic page exposes Search, Cancel search, progress and return to saved
discoveries. Original query syntax and rank/4 projection are used. Temporary
results allow Copy Magnet; saved read/delete/related-lookup commands are disabled
for them. Original RCM table configuration is retained. Selection/scroll survive
result refreshes, and unchanged result vectors are reused on telemetry updates.
The [screenshot](public-search.png) contains generated test data only. This is a
bounded integration into the existing classic RCM page, not a claim that every
original generic-search widget/provider, regex dialect or subscription/API
adapter is now ported. F12 remains partial and real-profile Public keyword
acceptance awaits a later user-requested build.

Validation: seven affected CTests passed in 147 seconds: database runtime
(19 Qt rows), shared chats (5), torrent DHT including original Java peers (12),
original cache/profile (143 assertions), shared discoveries including Java
associations (12 Qt rows), classic page (4) and matcher/wire differential.
The transport suite passed 23 Qt rows including actual original DDB Read and
Call peers. After the final immutable-projection change, the two affected UI/
matcher CTests passed again; matcher total is 5,769 assertions on the original
95-item/42-query corpus. An additional configured BiglyBT-network search data
row passed (three Qt rows including setup/cleanup); IPv4 and IPv6 were already
covered in the preceding owner suite. Remote numeric hint fanout, scope
rejection, source revocation, late replies, incoming Bloom and private source
suppression are exercised. Only generated profiles and explicit loopback peers
were used. The static desktop host also compiled, checking window/controller
bindings. No new sanitizer or release-platform claim is made by these runs.

No daily-use executable, preview manifest, running process, SDK binary or real
profile was changed. Compiler and detailed test logs remain local. Two GCC16
diagnostics outside this chapter were retained for a focused follow-up instead
of being suppressed. See the diagnostics task. Continue source chapters under
the latest autonomous instruction; prepare a user build only when requested.
