# Source-brokered Azureus cache forwarding runtime

Native `4fc79ab5f20079186247c534b173d62a018b7a16`, tree `e170c4ddba375dc3dce839fd70ae6d1d1b0f74c3`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Selection and DB prerequisites remain pinned in their linked records.

The actual normal UDP actor now prepares owning cached values, groups keys by
admitted destination ID, obtains original anti-spoof tokens and forwards only
non-local values. It uses actual seeded router/database state, no supplied seeded
or closest booleans. Original control sorts whole-bucket router candidates and
truncates to exactly K; the native cache path now does the same with failing
contacts included. Unknown or unproved protocol endpoints conservatively retain
the cache. Relay preserves origin/version/flags/lifetime/replication and creates
the actual local sender at final send time.

Every token request acquires a fresh normal source permit. Every STORE reacquires
the key-specific permit and checks the current content/store receipt. Source
withdrawal, revocation, replacement, close, expiry, foreign-thread or duplicate
callbacks cannot send a captured old value. Current source and receipt are checked
again for a conditional purge after the original final-forward stage. Partial
STORE/source failures retain the cache; failed tokens mark the actual contact
dead. Four contacts run concurrently, deadlines are bounded, and cancellation
clears captured payloads even when an old source callback remains held.

Normal profile/network lanes schedule the original30-minute main and one-hour
small-network periods, with the original fixed timer jitter. The five-minute
example in upstream DHTPluginImpl is commented out. The existing broker performs
all source/file validation; the UDP actor performs no profile I/O. A timing override
is confined to the explicit factory that verifies owned loopback SDK listeners;
it is never a saved or Public configuration override. Close uses the existing
cancellation path and does not add a shutdown grace period.

Qualification passed on three actually seeded native UDP actors, with both IPv4
and IPv6, issued tokens, two512-byte values within the1400-byte datagram limit,
actual recipient DB contents and local-publication exclusion. Negative cases
cover source withdrawal at the final key permit, an absent held permit, revoked
source, close, short expiry, a foreign thread and duplicate completion. Existing
cache state is explicitly preloaded for final-forward/drop tests because a new
incoming STORE correctly refuses an already nonclosest key. Late replacement
survives the purge receipt check.

A separate actual SDK/shared-listener/generated-profile run uses the real
ProfileLease/ConfigFile/AzureusPolicy/broker path. It forwards the expected value
and stops forwarding when the DHT flag in the actual config is disabled. It does
not substitute a test boolean for profile revocation. Final chat/NAT, torrent
privacy/lookup, original Java/native related-content/ratings, actual Stop/Pause
bridge and checkpoint wire regressions passed without skips. One application
link passed. Exact totals appear in [qualification.json](qualification.json).

This is a bounded runtime milestone, not whole F09 completion. The actor currently
refuses snapshots above8192 total values before allocation, sends at most two
values per datagram, and bounds a job to30 minutes. Large-cache paging/packing and
performance qualification remain required; do not infer whole-namespace bulk
parity. Explicit-replication-only values stay survey-only. Survey execution and
signed key-block maintenance remain open. Accepted tests use generated profiles
and owned endpoints; no daily image or live-profile acceptance was performed.
