# Azureus local diversification in the normal shared owner

Native `c75a7113457bd97d5cd5170562ce7ace06b874c0`, tree `40810465be530f43c76f189adcdffa9cdd5ef508`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Earlier checkpoint and DB prerequisites remain pinned by their linked reviews.

Normal profile/network lanes now hydrate original local `diverse.dat` records
before creating values, use original size/frequency/lazy-expiry policy with
system entropy for the original 2–3 day deadline, and keep the existing bounded
8192-key/4 MiB database. Original-mode entries per key are capped at256 so the
32 KiB size threshold can be reached. Local and remote records share the same
version/source namespace and preserve each other and unrelated fields.

An asynchronous UDP checkpoint hook holds at most32 responses with an explicit
2 MiB accounting budget and a deadline no longer than5 seconds. The actor never
reads or writes profile files. The existing broker validates the actual network
policy and performs the guarded atomic original-file merge. Replies are released
inside its current source permit only after the requested policy revision is
saved. Coalesced revision checks are constant-time while state is unchanged.
Periodic flushing also handles local changes outside an inbound RPC.

Delayed GET owns its data, reads the database once, reacquires the key-specific
permission, and checks pure policy/content receipts before sending. Withdrawal,
replacement, changed policy, revoked permission, failure, expiry, close,
destruction, duplicate and foreign-thread callbacks cannot release an old reply.
Synchronous already-durable responses keep the initial permit. Only actual packet
writes increment send counters. Request timestamps are encoded at final send.

The existing preparation window (at most750 ms) includes dirty DHT hints without
extending shutdown or claiming torrent payload progress loss. Failure cancels
pending acknowledgements; a retry is bounded and deferred for30 seconds.

Qualification uses generated data and actual owned UDP. The normal application
owner receives65 direct publishers with original issued tokens and distinct
restricted node IDs. Crossing32 KiB saves type3 before acknowledging; restart
retains type/expiry with volatile values lost. A pending save file prevents the
acknowledgement and preserves the original bytes. The restart key deliberately
keeps the receiver closest; original unseeded-forwarding and closest-node
admission remain enforced. Unknown and remote records survive both paths.

The expanded wire tests, normal owner roundtrip/failure/preparation, affected
chat/NAT, torrent privacy and diversified lookup, original Java/native ratings
and related-content checks passed without skips. Prerequisite18-case actual
original local fixture,8 profile checks and22 DB checks remain applicable;
unchanged green suites were not repeated. One final full application link passed.
Exact group totals are in [qualification.json](qualification.json).

This completes the local checkpoint chapter only. Forwarded-value republish,
maintenance completeness and broader F09 parity remain open; destructive obsolete
small-network repair is not executed. No user profile/payload was accessed and
no new daily image was published. The currently running daily stays unchanged.
