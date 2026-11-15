# Owning Azureus cached mappings and content receipts

Native `80ba30777345c37b442ca3f2ed2d8d9cb10cb88a`, tree `69746b383ab8baa90eca7238172c7e5450b113ea`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
This extends the [original selection prerequisite](../20261115-azureus-cache-selection/REVIEW.md).

The bounded Original-mode DB can prepare owning cached mappings after the original
forced expiry scan. It uses raw direct/forwarded entries, preserving duplicate
payloads and empty remote values. Ordinary reads, read counters, bloom/read
callbacks, new local versions, profile files and endpoints are not consulted.
Future store times are corrected on actual inserted mapping values by ID; an old
detached copy cannot change a current entry. Cached diversification headers retain
the qualified original lazy-expiry distinction.

Each candidate holds immutable lifecycle copies and a pure receipt combining
content identity with a monotonic store epoch. The epoch also catches original
silent same-version store-time resets without changing original adapter callback
counts or existing read-receipt semantics. A later change, removal or recreation
cannot authorise a purge through an old receipt. Conditional erase clears known
local policy through the existing dirty checkpoint path; it grants no network
authority. Actual current source/router/target checks remain the next integration.

Removing an own value now checks the raw remaining mapping rather than the
payload-deduplicating convenience read. A retained empty remote value therefore
survives, matching original getValueCount/getValues semantics.

Eight focused Qt totals and22 existing DB regression totals passed with no skips
or failures. Checks cover actual clock rollback and subsequent exact-age boundary,
raw duplicates/empty values, survey-only versus mixed local selection, owning
snapshots, silent duplicate-version resets, stale purge and recreation, own-value
removal preserving empty remote data, invalid/Absent/closed refusal and no clocks
for invalid inputs. The original19-case reference remains unchanged and was not
rerun. No full application rebuild, user profile or daily image was used for this
module prerequisite. The chapter remains in progress until normal forwarding and
its dependent maintenance parts are qualified.
