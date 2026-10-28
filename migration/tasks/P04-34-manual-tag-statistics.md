# P04-34 — Manual download-tag Statistics

Status: bounded package qualified and published; STOP at user request.

Final bounded qualification: GCC23 unique affected suites across repaired
attempts; Clang12/leak-enabled sanitizers12; hosted107 at
998db441c4f9d0ed1746ed564eec89f33ad541b9 (run39863152931), SDK39863152936;
private portable13 smoke rows plus2 suites,107 files/19 captures. Prior dist
retained. Product sources are unchanged by the four legacy tab-count repairs.
Full original service/automatic-tag parity is not declared complete. STOP.

Implementation notes and historical qualification progress follow.

Original baseline: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Pre-batch native baseline: `ede540798e690175204196c0cd40196ecaee7fdf`.
This slice follows the idle Statistics foundation, now qualified by this descendant;
it does not finish the tag manager.

Read persisted manual download tags from original tag.config type3, preserving
stable UIDs, raw names and declared canonical torrent hashes. Fixed-name held-
lease reads and normal input bounds apply. Declared hashes are not automatically
live resolved library members; download-state stag is unrelated swarm metadata.
The five-path parser/adapter/test capsule is staged only under
build/statistics-completion-20261022/tag-statistics; final manifest
`b0bf1440cc1317228e2faf788bd9214cc72f5bb1f6c98372da6aa8142d2cedba`.

Copy true per-torrent download_rate/upload_rate (data+protocol) from the already
queried lt::torrent_status. Preserve current LocalProgress payload semantics;
no extra engine request, thread, timer or owner-counter subtraction. Bind each
observation to the actual active target/hash and profile/run generation.

A bounded immutable projection and Qt table may aggregate resolved membership
only with actual target data and explicit producer-authorized owner coverage.
No provider means unavailable. Idle zero requires actual no-active-target
knowledge; completion or a stale last sample alone cannot certify it. Tags may
overlap, so their individual values must never be summed as session totals.
Hidden/minimized views defer painting, profile changes clear old rows, and stale
or reentrant delivery cannot restore previous-profile names or rates.

Opening Tags requests its read-only snapshot; it never starts a torrent engine,
tag rule manager, plugin, file update or network service. Auto tags, rules and
full manager callbacks remain distinct open features. Add a useful rates table
before optional history/charts. Root owns common desktop/worker/navigation
wiring; source-only agents own independent parser, sampler and view capsules.

Country remains separately blocked by absent per-peer observations and location
provider binding. A passive peer ledger is staged independently, with raw LT
carry-over and disconnect uncertainty explicit. It is not a Country provider or
an excuse to distribute aggregate session counters into countries.

Integration receipt: build/statistics-completion-20261022/manual-tags-batch/.
The source manifest is
`000d1462830ebb639ddf0ff13a6cabc319c8028637f22f4b633a1e0cd4d61486`.
The batch includes shared revision admission, lazy visible reads, Refresh/F5,
target-qualified combined/payload rates, original tag visibility and a same-Bridge
profile close/reopen regression. Library revisions are process-monotone;
successful close clears old profile admission before releasing busy state.
The existing single/multi worker rows optionally capture actual positive Tags
packets in owned temporary directories without adding backend reads.

Validation: build once, new six suites first, then eight affected regressions.
Secondary qualification repeats the six new suites and existing close faults.
Do not repeat unchanged Java arithmetic/history corpora or claim execution of
new tag Java oracles. Full original tag rules/automatic tags/history remain open.
