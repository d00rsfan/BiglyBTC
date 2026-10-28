# P04-10 Pure original classic column-layout projection

State: bounded pure implementation qualified locally and published; hosted desktop
CI is running. This task does not claim effective layout/UI/writer compatibility.
Upstream `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; native parent metadata
`0e96f6a5d8f6922791ab0655dc93b13e163366af`, source
`03e31e5ce260e1327e63cca63d22d8d5984ceae9`.
Audit: `migration/evidence/20261018-navigation/classic-column-persistence-audit.json`
(28 source files/60 complete source definitions); proposal freezes source formats,
legacy branches and per-column/runtime-registration limits.

Root selects a projection-only first checkpoint. No profile adapter, worker/UI
application, config writes, legacy biglybt.config read, registry initialization,
paths/payload/network/clocks, column mapping/normalization or effective geometry.
Keep the current dist/chooser/reset behavior intact. Separate adapter/UI admission
and raw-preserving writer belong to subsequent tasks.

API: const original Document input to expected owned immutable snapshot, with
named incomplete/complete tables, exact/default/absent/malformed provenance,
field-presence diagnostics and raw64/Java signed-low32 numeric facts. Preserve
all unrelated raw tables/plugin columns/userdata/list tails in one complete owned
Document, independent of later caller mutation/destruction. Reject edited or
raw/tree-inconsistent input. Enforce ordinary4MiB/100000node/depth64 parser maxima
regardless of higher-quota caller, checking the supplied tree safely before
recursive serialization. Default projection caps1024columns/selectedtable,
32sortIDs/table,256IDbytes,4096issues; reductions only, fail instead of truncation.

Use actual slots [visible,POSITION,WIDTH,...], without following the stale source
comment. Expose requested boolean/direction facts, never effective runtime column
settings. Preserve negative/duplicate positions, raw sort order and malformed
entries independently; no fabricated default/zero values. Exact empty and malformed
maps remain distinct from absence/default fallback. Original multisort precedence,
legacy unresolved flags and the source global-sort nullguard hazard must remain
explicit; never promote a convenient later known column or imply complete runtime
sorting behavior. Missing fields do not produce an issue per absent slot.

Ownership: codec authors only new core header/implementation after root freezes
API. Separate independent tests by oracle. Driver independently reviews source/
contract/production without own-code review credit. Root owns CMake, execution,
provenance, commit/push. Required checks: precise generated fixtures for both panes,
default precedence,low32,malformed/short/settings/ordered sort, opaque byte
preservation/lifetime, edited-input and reduced/maxima quotas; focused GCC16,
Clang24 and leak-enabled ASan/UBSan, then full native units. No original runtime
comparison or whole config-layout parity inferred from native fixtures.

Final header SHA256 `1d4d1c06efd71ef2ab026364eb15f434d4b8647d15f8ae31852052f63a996d39`.
Design: `migration/evidence/20261018-navigation/classic-column-projection-design.md`
SHA256 `b556cf736095ee573c9902ee6883b3c6fb4a1d25b7f39a27203160f20fd2cc3a`.
API pin record2d00da1c; independent projection-only source review1d8e25ed.
This approval authorizes only two new core files, no changes to old core/adapters.

Frozen typed-text clarification: every byte-string sort ID has Present plus an
owned ColumnLayoutText, even empty/NUL/invalidUTF8. Flags and EmptyId/Nul/Encoding
issues retain those facts without dropping/reordering bytes. Nonbytes are
WrongType with no projected value; Malformed describes bad shaped/member
structures such as RGB. Current-list invalidUTF8 emits Encoding; legacy singleton
records validUtf8=false without Encoding because default JVM decoding is unknown.
IDbyte cap covers the Column. suffix or raw sort ID. Requested slot booleans are
not effective registered properties. No scalar/code/runtime execution has occurred
for this new component yet.

Independent fixture plan frozen before implementation:
`migration/evidence/20261018-navigation/classic-column-layout-test-plan.json`
SHA256a132537138cbfdd5a1835985ee1d80caea1b159ac90ffb61f0367a4634d2af1d;
22planned families/112 finite planned subvectors, not authored/passed test counts.

Production source frozen at SHA256
`b55620a54cbdc243c8eccade512b19905d1efe819a4efb0db39bb435a325dd25`
(new388-line column_layout.cpp); frozen header1d4d unchanged. Core target builds
succeed with GCC16 Release, Clang24 Release and GCC16 Debug ASan/UBSan. Root
source review is `migration/evidence/20261018-column-layout/root-source-review.json`.
These are compiler results only; new fixture execution is still pending.

Recovered qualification checkpoint: source commit `5bf0a30160c52c51d8efce279cac758948adfff2` is
published on BiglyBTC main. Independent test source review1cf7b530 found no blocker.
All112 named generated cases/1628 assertions pass GCC16 Release, Clang24 Release
and leak-enabled GCC16 Debug ASan/UBSan. Full desktop unit suite passes35 entries,
zero skips. Exact source/commands/results are retained in
`migration/evidence/20261018-column-layout/local-verification.json`.
Hosted desktop run39122349333 pending; SDK39122349381 succeeds. Dist stays at
03e31e5ce260e1327e63cca63d22d8d5984ceae9 because this component changes no UI.

Scope note: fixture userdata key nameoverride is generic opaque data; it does not
qualify the original tablecolumn.nameoverride spelling or effective title changes.
Malformed raw keys/end extents are checked in production consistency validation
but have no dedicated fixture credit. Full original runtime/registered properties,
legacy global config/migration/defaults/reset/writes remain unverified; keep
component first_ported/integrated_through null for that broader coverage.

Hosted desktop 39122349333 and SDK 39122349381 both pass. Downloaded
artifacts independently confirm the expected35/36 units, focused case/assertion
totals, actual3-row preview smoke and all4 screenshots. See respective
hosted-verification.json. Public combined result record is committed at
`eefbd205ca0940ad4868ee07989b2d42be1a99dc`, tag
`milestone-classic-layout-reader-20261018`. Historical pending statements above
describe the checkpoints when written. No UI application credit for these tasks.
