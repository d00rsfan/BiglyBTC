# Rolling History GUI — independent source review

Outcome: **blocked on the agreed Ready-packet admission correction** below.
The staged tooltip lifetime patch is sound; no other concrete blocker was found.
This review read all six installed GUI/test files and the complete staged diff,
without executing/importing subjects, building, using profiles or changing sources.
The reviewer authored the core range implementation and grants it **no independent
review credit** here. Runtime qualification remains root-owned.

**GUI-A1 — inconsistent observation evidence is admitted.** In
`history_range_view.cpp:46–67`, a packet with zero present files can retain positive
whole observations/totals and satisfy admission. The UI then says “No saved files”
while displaying known numbers. The current observation cap uses the global file
maximum instead of `presentFiles`. Bucket observation counts are individually
bounded by whole observations, but their combined count is not bounded. The actual
core/reader cannot produce these forged facts. Root has accepted a narrow repair:
cap whole observations by actual present files × per-file records, and check the
sum of bucket observation **counts** against whole observations. Do not require
counter sums to equal whole totals: filename spill/query boundaries intentionally
make those numeric totals independent. New malformed DTO rows should prove the
checks before the amended source receives a final PASS.

**The staged lifetime repair closes the concrete old gap.** Installed render code
can return from the final DHT label's ToolTipChange after its owner was deleted,
then access `totals_.size()` or `query_`. The candidate's immediate `current()`
checks after both metadata and totals tooltip writes return before the next loop
condition/member access. The two added rows force a real final-label tooltip
change and exercise direct owner deletion and a newer failed request. The deletion
filter consumes the event; the newer request must retain unknown totals/plot.
Existing test bodies are unchanged after removing this additive block. Product
copy changes remove internal terminology without altering the accounting scope.

Other source findings:

- DTO settlement binds nonzero session/request, exact period/offset/now, monotonic
  request identity and a current loading request. Regenerated bounded plans,
  exact bucket count/endpoints, coverage and optional/count consistency are
  checked. Failed/contradictory/unknown-state packets clear prior displayed facts.
  The View relies on the declared immutable transport ownership of its shared
  packet; the Plot independently owns copied primitive bucket facts.
- All six total labels follow original order 0 protocol-up, 1 payload-up,
  2 protocol-down, 3 payload-down, 4 DHT-up, 5 DHT-down. Plot pairs are Payload
  [3,1], Protocol [2,0], DHT [5,4]. Whole totals are read directly from the whole
  result, never added from graph buckets. Unknown remains optional; zero and
  signed 64-bit values remain exact in text/tooltips/Clipboard.
- Plot construction rejects invalid plans/traffic and makes every bucket unknown
  if any supplied binding is inconsistent. It retains neither a mutable source
  DTO nor aliases of counter storage. Min/max include zero. Geometry converts
  signed endpoints to long double before subtraction, checks finite area/results,
  handles one bucket/zero range and does not subtract signed64 extrema as integers.
  Paint loops are linear in bounded bucket count, with non-overlapping x labels;
  line segments never cross an unknown bucket.
- Hidden/minimized View updates retain pending data/read intent and defer label,
  plot and read work until visible. Traffic selection advances render identity
  synchronously and renders in a QObject-bound queued turn, without re-reading.
  Period/offset/refresh reads capture one explicit UTC now; All disables offsets.
- Plot replacement, clear, selection, tooltip and Copy use QPointer plus frame and
  interaction revisions around direct QAction/property callbacks. Nested keys on
  the same frame invalidate old publication. Accepted replacement disables Copy
  and clears the cursor; canonical-name repair covers a key reentered while that
  action is being disabled. Clipboard publication copies exact selected values
  once in Clipboard mode, with no receiver access afterward. Focus/modifier and
  visibility gates protect inspection; focus loss retains selection.
- Tests substantively cover all 18 period/traffic combinations, independent
  whole numbers, unknown/zero, stale admission, hidden/minimized deferral, extrema,
  source mutation after Plot construction, actual shortcut/Clipboard behavior and
  direct request/tooltip/Clipboard deletion or reentry. The staged extra two rows
  address the discovered final-tooltip gap. They are source expectations here,
  not observed test results. The new type does not gain runtime action/accessibility
  reentry coverage merely from similar older Plot suites.

Exact input hashes (installed images and all staged artifacts) are frozen in
`review-gui-oracle-inputs.json`. Principal pins:

| Input | SHA-256 |
|---|---|
| presentation header | `2301375a1f8328bd4e09d98f63f879feaab92e209090b9c41e84a807f42db62c` |
| View header | `64cfb338ffd1be636e90762effd990e1edb3995b7a498fe300316cdc2e97de41` |
| installed View | `d16de14fa625ccf7f0f22fd9fe48e28570435eb0bd746bb98e3ffd6c430aa42f` |
| Plot header | `7315dfef929f525c59ad89f91bdb770adf7d208e84cda629c7aee72a3e6135d5` |
| installed Plot | `37b740ef07fafae33cefe1633c5ed837e26b9758153476565c5ad2c002f72910` |
| installed test | `824d6b64646e71de64588a3625314d972d45dfebd179d4d43c06a4d9902ffe87` |
| staged View | `243f1589f7efb8df8508e1fb89e99b5bd5faaf32c5c91297cca257b4a0b32536` |
| staged test | `66045961e9964ecab4e91a4be2e82c8e07ef37f9b1eade30e13f6249efb239f8` |
| candidate patch | `3f0b574c54a67aaf1ef9e320498c9fc4ee1157e1d1fdd078d6169c96c2409a23` |
| amendment manifest | `ccf7bd8278199c5c22a353865dba90932136e5b88180a942e85665ce921e6a42` |
| contract | `0cd89591df1d1941800622bd2d58851fc603676bd2946eaf22689651f090c15d` |
