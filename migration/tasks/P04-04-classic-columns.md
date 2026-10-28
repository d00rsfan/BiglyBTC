# P04-04 Classic torrent column controls

State: bounded session-only controls implemented and locally tested; original
column editor/settings compatibility remains open.

Original baseline: BiglySoftware/BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, particularly
`TableColumnCreator.createIncompleteDM/createCompleteDM` and
`TableViewSWT_Common.fillColumnMenu`. Full source digests are retained in the
source proposal and the public `preview/docs/classic-columns-source.json`.

Public native implementation: `7458daa2bf576e872282166e296373c1454504f7` in
`https://github.com/d00rsfan/BiglyBTC`. The table-owned ClassicColumns helper
offers the nine existing logical model columns through each header popup and
separate View submenus. All-hidden recovery stays reachable there. Reset restores
construction-time order/widths and preview default visibility, preserving sort,
source model, target identity, filter and selection. Incomplete now displays both
rate columns; complete hides download rate. Unsupported columns are not invented.

Root GCC16 build and all25 native unit checks passed, including13 actual column
Qt rows. Generated-profile smoke passed at100/150/200% scale, preserving inventory,
cache/state bytes and empty payloads. Existing navigation/model/lifecycle checks
passed unchanged. An independent source review found no blocker for the fixed
construction-time schema. Actual reports and hosted status belong in
`migration/evidence/20261018-columns/verification.json`; source review is under
`migration/evidence/20261018-navigation/columns-review.json`.

`~/Java/BiglyBTC/dist` now contains this build, and previous folders are retained
for rollback. Existing preview processes were never stopped by folder promotion;
all82 runtime dependency files were proven byte-identical. Source changes do not
modify the original Java application or its profile.

Open work: original table configuration import/save, complete editor semantics,
dynamic/plugin schemas, runtime localization updates, actual compositor popup
and accessibility qualification. Session-only native controls do not close those
gates or qualify full UI/client/profile/performance parity.
