# P04-06 Saved queue-position column

State: verified bounded source milestone `ec56329fdfab1a45a8eafd7bacc97c163d5815c2`
is committed/pushed, and actual hosted/local31-unit checks pass. Dedicated17 Qt
rows and leak-enabled ASan/UBSan checks pass, with three-view scale smoke. Full
live Rank/scheduler/DND/profile/performance parity remains OPEN. Original baseline
remains `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Audit: `migration/evidence/20261018-navigation/next-library-columns-audit.json`
pins12 original and20 native files. Root independently checked every committed
blob/hash. The original Rank is a live manager int position, subject to startup
narrowing and group renumbering; it is not the raw saved config value.

Implement a read-only “Saved queue position” column at logical9, preserving
old logical0–8 and the current sort. Keep raw optional signed64 from existing
SavedDownload.position: absent is distinct from zero, negative/full-width values
are not clamped or renumbered. UserRole sorts numerically. Snapshot authority may
replace/clear the value; detail/telemetry updates must not invent or alter it.
Both panes show it initially and expose it in their independent column menus.
No parser change, scheduler policy, profile writes or live Rank parity claim.

Before publication: dedicated model/GUI/numeric/selection tests plus a real
Bridge held-profile generated-config read with unchanged input bytes/identity;
update exact header/column test expectations without dropping their assertions;
full native units, scale smoke, independent source review, commit/push/provenance.
DND-aware Size/Remaining/Progress and full live queue semantics remain OPEN with
the source audit's specific per-file/statistics prerequisites. This small slice
neither completes nor removes those requirements from the migration plan.


## Verified source milestone

`ec56329fdfab1a45a8eafd7bacc97c163d5815c2` is committed/pushed. All31 native
unit checks pass, including17 new Qt rows with actual generated-profile Bridge
reads and unchanged bytes/identity. The new suite also passes ASan/UBSan with leak
detection. Three-view preview smoke passes100/150/200%. Actual hosted desktop
run39115097757 passes31 units and three-view smoke/captures; SDK39115097703 passes.
Exact file/source/evidence hashes are in
`migration/evidence/20261018-saved-position/verification.json`. Current dist has
103 verified files with82 identical runtime dependencies; predecessor retained
under native `build/saved-position-20261018/previous-dist`. No process was signaled.
Independent source/coverage reviews pass, including separate regressions for raw
-1 and optimized same-row refresh clearing. Full live queue/DND/profile/performance
semantics stay open. No Java-runtime equality or real-compositor credit.
