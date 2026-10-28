# P04-11 Held-profile classic column-layout reader

State: root authorizes independent source authoring against frozen P04-10 API;
no build/execution/integration claim. Implementation/test registration follows
the pure-core checkpoint. Source authority remains original
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Native current parent
`0e96f6a5d8f6922791ab0655dc93b13e163366af`; new pure header1d4d/cppb556 are
separately frozen but not committed/qualified yet. Record the actual P04-10
implementation commit when available, never an invented future hash.

Frozen source-only proposal:
`migration/evidence/20261018-navigation/classic-column-layout-adapter-proposal.md`
SHA2563f0599f3d2a61a35b02838648a9b86e0048a46e58bee90a29cf23c3e382f453d.
Root chooses two new files in the existing profile adapter module, following the
saved-service reader pattern; no generic loader/name-validation/refactor change.
CMake registration is root-owned after the pure source is committed.

API: storage::ColumnLayoutsView contains optional<core::ColumnLayouts> snapshot,
optional<RecoverySource> recoverySource and missing()==!snapshot.has_value().
readColumnLayoutsView(const ProfileLease&,std::stop_token={}) returns expected
of that view/Error. Hardcode only tables.config plus existing exact .bak/.saving
candidates. Call ConfigFile::load with fixed core::Limits{}, then pure
readColumnLayouts with fixed defaults. Caller lease is borrowed synchronously;
no acquisition/release, caller filename, global config, directory scan, saved path,
payload, clock, UI/worker or save/rewrite/recovery action.

Preserve existing candidate ordering and unsafe/read errors. Only on loader
Missing confirm exact three-candidate absence with bounded readFile calls; a
present malformed recovery candidate is InvalidFormat, never an absent layout.
Separate reads are not an atomic external filesystem snapshot. An empty loaded
root still has engaged snapshot/recovery source, distinct from missing. Selected
malformed table/field facts remain a diagnosed successful pure snapshot.

Check stop before touching the lease, after whole loader call, before/after each
absence-confirmation read and after pure projection. No in-read cancellation claim.
Discard state on cancellation. Map pure Limit->Unsupported, Memory->Io,
Root/EditedDocument->InvalidFormat. Fixed messages; catch boundary bad_alloc with
observed cancellation priority. Result owns all raw source and copied IDs, without
retaining lease/fd/local ConfigFile. No generic readLibrarySource/startup hook.

Ownership: codec only new adapters/column_layout_view.hpp/.cpp; driver only new
independent tests/column_layout_view_tests.cpp. Root owns CMake/execution/provenance.
Meaningful generated-profile fixtures: missing/empty/diagnosed data; all actual
primary/backup/saving precedence and malformed recovery ambiguity; unsafe/oversize
errors plus ignored unsafe unused candidates; pre-cancel and deterministic stop
following a real matched file read; exact byte/dev/inode/mode/nlink/mtime preservation
(except atime); retained snapshot after replacement/lease and fixture destruction.
No normal profile, network sockets, original JVM or payload access.

P04-10 is now source-committed and published at `5bf0a30160c52c51d8efce279cac758948adfff2`;
three focused lanes112cases/1628assertions and full35units pass. Adapter production
is frozen hpp eee7b06a10d3b8ebddbecad07c50344b09953d0273bfc0b0129692cc23cb96ea /
cpp d4bc7a9008acd645de68a80c2ecfa8bed2e18deb1272e4c802c0e5e70396288e.
Independent source-only review124893ca and independent test review16c29685 find no
blocker. New547line fixture e283f5dacacf20725d249add26424e7bb0d7885b512a6224167b869dcfc232df
defines53 generated-profile cases. Root registered only the new cpp in existing
profile target and target-private --wrap=read test target after pure commit.
All three targets compile; execution has started and is not yet credited here.

Recovered implementation checkpoint: `51ce95d150fa352ef81ef18c6f318c7ef23642b4` is source committed
and published. GCC16/Clang24/leak-enabled ASan+UBSan each pass53 cases/6894assertions.
All36 desktop unit entries pass, zero skips. Exact source/build/execution records
are in `migration/evidence/20261018-column-layout-view/local-verification.json`,
and committed-byte verification in source-commit.json. No adapter source/harness
fix was required. UI/startup/writer behavior remains outside this reader task.

Hosted desktop 39122570716 and SDK 39122570686 both pass. Downloaded
artifacts independently confirm the expected35/36 units, focused case/assertion
totals, actual3-row preview smoke and all4 screenshots. See respective
hosted-verification.json. Public combined result record is committed at
`eefbd205ca0940ad4868ee07989b2d42be1a99dc`, tag
`milestone-classic-layout-reader-20261018`. Historical pending statements above
describe the checkpoints when written. No UI application credit for these tasks.
