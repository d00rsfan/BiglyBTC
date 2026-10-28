# P04-08 Pure saved DND aggregates

State: bounded pure/cache implementation verified locally in native
`aaa7a1f4a84b66c0edc3d900c701e38a81fcfd2b`. All33 native units pass with zero
skips;61 independent cases/464 assertions pass GCC16 Release, Clang24 Release
and GCC16 Debug leak-enabled ASan/UBSan. Publication/hosted observation is recorded
in the verification receipt; no Java manager/runtime or UI parity credit.

Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; current native parent
`efdf27688594ceecd6d27f8bc2c6e7c0e4c5ce4c` (metadata for source
`4bfa2d85c9154ac3ede33c8298d2d7209d6589c5`). The relevant native cache/core bytes
are unchanged from the contract's separately pinned earlier native baseline.
Focused source and minimal API policy:
`migration/evidence/20261018-navigation/saved-dnd-progress-contract.json`.
Independent exact math/default/flag/precedence review:
`migration/evidence/20261018-navigation/saved-dnd-math-review.json` pins19 original
files and44 complete method bodies. Original source remains unchanged.

Implement only fixed owned saved-attribute facts and a pure allocation-free,
read-only arithmetic projection in `saved_dnd.hpp/.cpp`, appended to CachedTorrent
and parsed during the existing state-cache pass. No UI/model/Bridge/adapters,
config writes, recovery, path/payload access, directory scan, clocks or networking.
Do not change prior base cache parsing errors, limits or last-entry-wins behavior;
new DND child fields must not poison otherwise-valid names/size/cache metadata.

Default/policy boundaries:

- Missing skipfss K defaults to original−1 and requires facade recovery; pure
  projection leaves dependent metrics unknown with recovery/default provenance.
- Missing skipbdl B inside valid attributes defaults to original0 with presence
  and default provenance. Malformed values/maps remain unknown separately.
- Cache T<=0 would trigger original fixup; cache-only source-equivalent metrics
  stay unknown. Missing saved completed C is unknown, not a fabricated0.
- Keep selected size/remaining/Done, literal cached flags and no-DiskManager
  controller candidates separate. S0 Done1000 is an independent early branch;
  it never establishes public assumedComplete or complete/incomplete pane policy.
- Match original binary32 evaluation order and999 safeguard for rounded1000
  with positive remaining. Avoid C++ signed overflow/invalid conversions.
- Strict invalid/cross-counter relationships yield affected metrics unknown,
  retaining independent size/flag facts; document differences from Java's
  malformed/stale-data clamps rather than claiming whole-runtime equality.

Before verification: independent core/cache fixtures covering source defaults,
missing versus explicit flags0, binary32 boundaries, strict diagnostics, partial
known results, limits/duplicate identities/base parser error compatibility and
source document immutability. Root owns CMake/builds, GCC/Clang/sanitizer execution,
full native units, source/provenance publication and commit/push. Copied expression
characterization is not Java manager/runtime equality. Full selected-row cache
precedence/geometry/per-file recovery, UI, pane config/assumedComplete and original
profile/performance parity remain OPEN next stages.

Actual local reports and source pins: `migration/evidence/20261018-saved-dnd/verification.json`.
Independent implementation/test reviews found no blockers. The broader proposal
originally included an inert Java helper before implementation; root approved
this narrower source-derived first checkpoint instead, without changing the
frozen proposal or assigning any Java equality credit. The exact scope resolution
is `migration/evidence/20261018-saved-dnd/proposal-scope-resolution.md`.
An inert exact-source-body probe and UI integration audits are now separate work.
Dist remains the validated Close Tab source4bfa; this pure API adds no visible UI.

Hosted desktop39118014131 and SDK39118014174 both passed. Downloaded desktop
artifacts independently confirm33 native units/zero skips,61 DND cases/464
assertions and the actual three-view smoke/captures. Public provenance commit
`9e890be4c6096934ab27c786ef37f3f706d67f94` records implementationaaa7.
[P04-09](P04-09-saved-selected-columns.md) advances optional labelled columns;
all broader completion/cache-recovery/runtime gates remain open.

Later original exact-source-body probing completed after the native draft, with
157cases/1263calls (1179OK,43predeclared source type errors,41 explicit unavailable
file-provider boundaries,0unexpected). The frozen137-row actual-core comparison
matched all869 selected scalars:555 mapped operations pre-Java plus314 independent
fields mapped after Java/before native. No comparison subjects were rerun to fit
outputs. Exact bodies/fragments/inert hooks, exclusion policies and chronology
remain explicit; full manager/pane/disk/recovery qualification still gets0credit.
Retained complete raw artifacts and hashes:
`migration/evidence/20261018-saved-dnd/source-body-followup-inventory.json`.
