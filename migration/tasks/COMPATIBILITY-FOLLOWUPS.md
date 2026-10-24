# Open compatibility work after the everyday torrent preview

Read this checklist alongside MIGRATION_PLAN.md and STATE.md when selecting or
closing a milestone. A qualified preview does not close these items. Keep an
item open until its implementation commits and applicable interoperability or
runtime evidence are linked here; split partially completed items explicitly.
No item currently requires a user answer.

Sources: [chapter review](../evidence/20261024-torrent-workflows/REVIEW.md),
[detailed scope](../evidence/20261024-torrent-workflows/scope.md), and
[qualification receipt](../evidence/20261024-torrent-workflows/verification.json).
Qualified native source: `08a44ec3134e32c8f1cebc4061f5fa608ba2cc35`.
Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

- [ ] **CF-01 — Additional storage layouts (P03/P11).** Implement compact,
  reordered and remaining unsupported original mappings. Qualify partial and
  complete generated Java/native/Java handoffs, file identities, incomplete
  names and failure recovery. Preserve explicit rejection until supported.
- [ ] **CF-02 — v2/hybrid profile interoperability (P03/P11).** Extend actual
  partial/complete Java/native/Java reload checks beyond the two qualified v1
  cases. Cover full identities, alternate hybrid identities and broader original
  profile options without losing unknown fields or payload progress.
- [ ] **CF-03 — Network settings and routes (P04/P07/P11).** Complete original
  bind/interface/proxy behavior and its settings UI; qualify applicable TCP/uTP,
  IPv6, DHT/PEX, web-seed and private-torrent paths. Verify route/DNS behavior and
  failure isolation against controlled endpoints. Tor/I2P depend on their P07
  integrations. Translation alone does not establish runtime compatibility.
- [ ] **CF-04 — Remaining queue/seeding providers and Scheduler recovery (P08).**
  P08-01 now qualifies ordinary deterministic ranks/FP/ignore/limits/minimums,
  persistent manual queue authority, compatible settings and Speed Scheduler
  editor/rate/pause integration. See [review](../evidence/20261024-queue-seeding-scheduling/REVIEW.md).
  Still implement/qualify lightweight/reserved/flexible slots, active speed/ETA
  probes, external/tag-specific FP/limits, disk-latency/smoothed-rate/stale-rank/
  file-priority observations and new-seed/group saved-position normalization.
  Retain exact tracker announce/scrape/self-exclusion, original duration rounding,
  extreme share-ratio overflow projection and within-cycle bandwidth differences.
  Add supported inherited-pause/interrupted-intent recovery and its editor;
  legacy records currently remain untrusted for automatic resume. Extend actual
  original traces/owned payload handoffs before advancing whole-component parity.
- [ ] **CF-05 — Remaining intake/file options (P04).** Add original pre-add
  per-file/options controls and supported priority changes during active work.
  Qualify saved original priorities, selected completion and actual payload IO.
  Current stopped-file controls remain the existing supported boundary.
- [ ] **CF-06 — Remaining library/file operations (P03/P04/P08).** Implement
  removal versus deletion, relocation, torrent creation and associated dialogs.
  Verify intended confirmations, original metadata consistency, recoverable
  failures and preservation of unrelated files using generated data.
- [ ] **CF-07 — Remaining engine/plugin integration (P05–P10).** Continue the
  feature/component inventory in the main plan. This chapter does not close full
  original engine/plugin parity or unavailable Statistics providers. Record
  individual dispositions and source/runtime evidence in their component rows.
- [ ] **CF-08 — Wider runtime and performance qualification (P04/P11 and final
  qualification).** Supplement offscreen evidence with Linux compositor/scaling
  checks, applicable public-swarm interoperability and comparable CPU/memory
  measurements. Track the private counter shim's vptr sanitizer exception until
  a supported ABI/provider change permits its removal; other checks remain on.

The compatibility target remains opening the same original configuration and
torrent state in either client after closing the other. Use generated isolated
profiles for development; never test against the running original profile.
The [Tools → Options chapter](P04-OPTIONS.md) qualifies50 original settings,
compatible sparse saves, safe live limits/native upper queue caps and default
intake-directory selection. CF-03 remains open for additional routes, interface/
proxy and LAN policies; CF-04 remains open for the remaining providers and Scheduler recovery. Original-only
new-import naming/automatic-speed settings remain explicit.
The [qualification review](../evidence/20261024-options/REVIEW.md) records limits.

[P08-01](P08-01-queue-seeding-scheduling.md) extends the catalog to100 fields and applies ordinary queue/minimum-download rules; source-only controls remain labelled.
