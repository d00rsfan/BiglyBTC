# P04-12 Read-only classic saved-layout UI import

State: locally qualified, source published and dist promoted; hosted CI pending.
Implementation began against published pure projector
5bf0a30160c52c51d8efce279cac758948adfff2 and held-profile reader
51ce95d150fa352ef81ef18c6f318c7ef23642b4. No execution/UI qualification yet.
Original authority cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; frozen proposal
migration/evidence/20261018-navigation/classic-column-layout-import-proposal.md
SHA256 c79b33b0708db00c7f1e3835c94d777fc56b761cf5538b908fabe39939859783.

Root approves a narrow read-only initial import and explicit Reload saved column
layout action. Ordinary library refresh never reloads headers. Six exact layout
IDs only: name0,status1,downspeed4,upspeed5,up7,torrenthash8. Preserve unknown and
incompatible IDs only in raw source, no private saved/full-value aliases. Widths
16..8192 except responsive Stretch Name; explicit visibility; order uses unique
nonnegative positions, compacted ahead of other native sections retaining relative
order. Reject duplicate order facet independently. Only singleton current
SortColumns with exact whitelisted ID and explicit nonnegative direction applies
native UserRole sort; no legacy/global/multisort promotion or Java row-order claim.

Use separate immutable session/request packet and per-pane local edit revisions.
Initial import only when no local edit; explicit reload captures current revision;
any later resize/reorder/chooser/sort/Reset wins per pane. Suppress import/internal
revision changes and automatic Name Stretch resizing. Reset always expresses local
intent even when no-op and restores original construction baseline. Busy/pending
requests coalesce, cancellation/no packet releases request; stale session/request,
close/deletion/reentry cannot apply or retarget state. Retained CtrlW My Torrents
widget keeps headers/filter/selection/details without reattachment or backend work.

Codec owns ClassicColumns/LibraryWindow plus new DTO; Driver owns SessionBridge;
Oracle owns new Qt/generated-profile tests independently. Root owns main wiring,
CMake, builds/tests, evidence/provenance/commits. Do not touch frozen reader/core,
engine/value policy or existing tests without a concrete affected expectation.
Use generated fixtures only; do not open the running Java profile or signal it.
Required native Qt admission/lifecycle and Bridge fixtures, full unit regression,
focused leak-enabled sanitizers, actual isolated preview smoke/captures. UI state
must be visibly checkable before dist promotion. Broader registered-column
semantics, config writes/Java roundtrip and whole-client parity remain open.

Current implementation parent is metadata commit
`eefbd205ca0940ad4868ee07989b2d42be1a99dc`; both predecessor hosted matrices
pass and the public read-only reader milestone tag is pushed. Original source
and frozen pure/adapter code unchanged. Shared DTO447fed4086b0551601d324f42dd5de39eda2eb51ee476ab63343a6d02238cf83
is frozen; synthetic UI snapshots must own nonnull rawsource as production does.

Worker source freezes: session_bridge.hpp5044106852db60f4b4d10873576acfa24b2107dba7f3c422976ecbdf9a5a4209,
cpp07daab4bffaf61e83fa603eaabf7bc048c3f8aa3ec123e84ed64f8b5a2fce00e.
Root static review passes; separate independent review is finalizing. Narrow
dispatch recovery=false applies only to this optional reader, avoiding generic
library refresh/revision on cancelled/failed reads; old command behavior remains
the default. Sixteen complete old definitions are byte-identical.
Independent plan885939fd has21 UI/8 Bridge groups; preliminary UI23methods and
Bridge14behavioral rows are source authored, not yet executed. Root added two
explicit synchronous header deletion/Reset regressions before execution; final
hashes/row counts will come from that expanded freeze and actual run.
Root wired real desktop/smoke and generated original-format tables.config sample
(Exact incomplete status124px, default complete132px, StretchName420requested
width intentionally ignored). Smoke will check actual automatic import and
explicit reload with no selection/detail retargeting or byte changes.

Actual implementation commit `2b09f1ec75271fe6cf59e47f895087e041beff10` is published. Full38unit
entries pass, zero skips. New30Qt UI rows and16worker rows pass, and all46 also
pass leak-enabled ASan/UBSan. Actual generated-profile import/reload, selected
Target/detail preservation, original config-byte preservation and three-view/chooser
smoke pass at100/150/200% scale (3Qt rows/4captures each). All3 classic captures
were visually reviewed. No original Java/profile/process was touched.

Root source-history records retain independent shadow-name and predicate-lifetime
findings and failed/no-owner dispatch fix; all were resolved before first UI test
execution. Initial sanitizer build command requested a packaging-only target absent
in that configuration; exact failed command log is retained, then target list fixed.
No flags/checks/timeouts/fixtures were weakened. See
`migration/evidence/20261018-classic-layout-import/local-verification.json`,
source-commit.json and dist-promotion.json.

Durable dist source is now2b09f1ec75271fe6cf59e47f895087e041beff10; all103
manifest files verified and82 runtime files byte-identical. Whole previousdist
retained at ../BiglyBTC/build/classic-layout-import-20261018/previous-dist.
No system installation or process signaling. Hosted desktop39124443099 pending,
SDK39124443064 succeeds. Broad original config-write/runtime equivalence remains
open; no first_ported/integrated_through advancement for that broader component.

Hosted desktop39124443099 and SDK39124443064 now both succeed. Downloaded
artifacts independently confirm38 native units,30+16 newQt rows and actual
3-row smoke/all4captures. Combined verified result metadata is committed at
`ba28f998f3709cbfc692b74c30b634b576ffef20`, tagged and pushed as
`milestone-classic-layout-import-20261018`. Earlier pending lines retain history.
