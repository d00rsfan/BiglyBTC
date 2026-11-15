# Bounded owning survey candidates

Native `fec5460eab04dc7d447c6d7aef74b099bafb50aa`, tree `0a771d322cf8c1a9cff74d5bda40e23b1ec8b40b`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The DB has an independent survey cycle that performs the original non-forced
expiry scan, freezes keys/incarnations plus a store-epoch fence and returns
bounded owning pages of raw values. Defaults are 64 examined keys and 256 values;
validated configurable input bounds remain finite. All live record identities
are available without retaining namespace payloads. Changed, deleted, recreated
or newly inserted records cannot become an old frozen subject. Current owning
projection separately requires exact incarnation/content/store receipts. Pages
preserve local, duplicate and empty values and the cached Mapping diversification
header, without ordinary read, new clock/counter or future-store-time repair.

Qualification covers 8385 empty remote values across 65 keys, bounded returned
pages, one expiry clock and zero normal reads. It verifies raw local/duplicate
values retained after deletion/close, stale store/identity refusal, frozen-set
supersession, independent cache/survey cycles, original non-forced expiry timing,
unchanged future store times, diversified headers, invalid cursors/bounds and
close/unsupported-mode refusal. Defaults bound returned pages; a temporary next
record projection is separately bounded to 256 values. No namespace-sized value
snapshot is created.

Focused GCC16/C++23/Qt6.12.0 candidate, actual-original state, DB, cached-value and
query-index checks passed without skips; see qualification.json. No application
link was repeated for this currently inactive preparation API; no new daily or
live-profile acceptance is claimed. Next work remains saved original feature-flag
activation, state/LRU ownership, neighbourhood lookup, query aggregation and
current-source token-first repair forwarding. F09 remains in progress.
