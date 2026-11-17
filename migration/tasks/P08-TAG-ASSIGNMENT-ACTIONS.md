# P08 actions on tag assignment

Original baseline selected before implementation in the retained exact-source
oracle manifest: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.

Port Start/Resume, Stop/Queue/Pause and Force/NotForce assignment actions, preserving
state/metadata/destroyed guards, branch priority, dispatch order and execution-time
flag reads. Exact extracted original branch uses recording async/DM boundaries;
it does not certify the whole addTaggable, scheduler or real command ownership.

Next connect strict saved pp.<action> flag reading and actual assignment receipts
to the existing normal serialized Host/ManualTagStore/engine command owners.
No startup/load/reload replay. Only a real committed fresh membership change may
create a dispatch receipt; no-op, failed, cancelled, recovered, stale or foreign
sources must not create effects. Existing generic store remains strict until an
actual actor handles these actions. Preserve unknown/unsupported scripts, hosting,
file moves, option templates and nested-tag actions without guessing effects.
Explicit command execution must leave the filesystem gate before engine calls.

The pure lifecycle protocol is not an executor, writer grant or whole F13 finish.
Continue autonomous plan work; no new daily, real-profile access or client launch.

Pure lifecycle assignment protocol qualified at `e5eaf9ca32a36ee3d31a1c6d118601149b5aef3a` / `aab0ac4a48717d34ecb56a4bf76fc07dd18f8c56`;1795 exact extracted original traces and current edit/constraint controls pass. Independent original BEncoder INT32_MIN crash reproduced and Native11-boundary regression passes. [Evidence](migration/evidence/20261117-tag-assignment-lifecycle/REVIEW.md). Actual saved source/assignment receipts/normal command dispatch remain current work;no active executor or whole integration claimed. Continue autonomously without daily/live-profile activity.

Saved assignment-action getter projection qualified at `aa8c16afc106a72d1902e437ac76afadf7875a40` / `ec9eff3be5f42c9e240ddb38e06e4571f9beb15f`;689 actual supported/enabled cases and1795 lifecycle traces pass. [Evidence](migration/evidence/20261117-tag-assignment-flags/REVIEW.md). Corrected original strong-config cache fixture consumed without rebuilding unchanged Native code. Generic writer/effect guards remain strict;actual normal assignment receipts/dispatch are next. Continue without daily/live-profile activity.

Actual source/writer assignment owner qualified at `9bd75160d0b937e37c008e93f8bd4cba765e71f4` / `c091da54d419104cfc5664c27860763dd2b05380`;eight actual profile/CAS/receipt families,current689 getter/1795 lifecycle traces and existing identity/store/edit controls pass. [Evidence](migration/evidence/20261117-tag-assignment-owner/REVIEW.md). Private actor path only;public writers remain strict. Receipts are once-only,weak actor/store scoped and bounded32;load/reload produce none. Normal Host/UI producer and actual existing-worker engine dispatch are NEXT;continue autonomously without daily/live-profile activity.

Normal manual assignment lifecycle dispatch qualified at `edbd9075bf0a3ed89300010f6c2408e9b1a392b5` / `8b5820a59e78d8519212136cfebedf4fda63f179`;actual normal Host Force/Pause/Resume/queued retirement/Stop chain,no-replay/offline/missing-resume negatives,36 original stop plus36 queued-setter results,and affected UI/source/state-history/24family986assertion controls pass. [Evidence](migration/evidence/20261117-normal-tag-assignment-actions/REVIEW.md). Normal Public manual producer only;automatic constraint/tracker/Untagged/intake and dependency-plan effects are NEXT. Other effects/transient owner limits and whole F13/global baseline remain open. Continue autonomously;no daily or live-profile work.

Automatic constraint lifecycle actions qualified at `2825b1c22204c7a6798a738e2fc7f2048e0971be` / `bc1d2611813265f87031d26ce432e4c4ef2e7f41`:actual normal Host fresh assignment/SDK Force Start/reopen no-replay,eight adjacent observation/actor controls,35 lifecycle schema cases and affected source/group/peer/metric/UI controls pass. [Evidence](migration/evidence/20261117-automatic-constraint-assignment/REVIEW.md). Ordinary/direct-intake constraint producer only;tracker/Untagged/birth/dependency-plan and other effects remain current work. Continue autonomously without daily or live-profile activity.

Atomic automatic lifecycle producer integration qualified at `1a2f60e9b7e440b8ea530a0b2e7f133ef297fadf` / `127a7d419195b559dd7e97d3ca55b109b33aeda8`:ten actual owner families,32-tag/64-task one-CAS ordering/no-prefix/strict public-plan guards,normal tracker/Untagged,genuine tracker-birth/new-only dependency action dispatch and rebuilt shared UI/source regressions pass. [Evidence](migration/evidence/20261117-automatic-tag-action-batches/REVIEW.md). Current supported Public owners only;other effects/transitions,large-cohort efficiency and whole plan remain current. Continue autonomously without daily/live-profile activity.
