# Immutable membership plans and source-bound staged facts

Native source `ba60e9e6511767d160bf4621aeb1f99aec9eb319`, tree `61a96bd983241a99c3f4247b69dc8c7204ac967c`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f` for tag membership/config semantics.
This qualifies Native preparation/source guards, not a fresh Java runtime.

An immutable plan retains its actual ManualTagStore snapshot and an owning view
of staged tag values. It owns no profile lease or writable physical snapshot.
Extension accepts only bounded typed membership changes and repeats ordinary
supported-effect policy. Previous plan views remain unchanged; no-op extension
retains the same view. Source/generation/writer lineage is private and cannot be
supplied by a caller. The32-change cap remains in force.

Publication replays the ordered edits through the existing actual owning store
and compares replay bytes with the sealed plan view before any save. Actual
candidate/primary preimage, profile ownership, subscription/recovery restrictions,
cancellation and synchronous CAS still apply. A committed base, explicit reload,
foreign profile, closed/reopened writer or uncertain owner cannot publish its old
plan. Plans and fact snapshots do not keep the profile lock alive after close.

An optional staged-fact path retains the actual physical tag snapshot separately
from virtual membership. It validates exact target/session/turn, current real
store/base lineage and immutable plan view. Reuse across different staged views
fails; selected-own age is recalculated from the current view. No physical source
or previous facts change during staging. A staged absence remains unavailable
under the incomplete global census, rather than becoming a false removal proof.
Evaluation still requires the current real store/snapshot and supplies no effect
permission. The normal worker continues using the unchanged no-plan path until
a separately qualified recursive consumer is connected.

Sixteen actual isolated store families pass: all previous fourteen plus immutable
view/staging, exact one-publication replay and backup, stale base, cancellation,
foreign/reopened owners, quota and actual lock-release tests. Source-fact checks
prove staged removal/addition/age, retained physical facts, unchanged file bytes,
changed-view refusal and equal-byte reopened-writer rejection. Normal Host rules,
state/category consumers, disabled startup and stale/foreign/cancelled/closed
workers pass. GCC16 Release/C++23/Qt6.12.0 focused builds and one coherent application
link pass. No sanitizer or real-profile run is newly claimed. Agent self-review;
no independent reviewer is claimed.

No live profile, payload, running client, protected SDK, daily launcher or rollback
image changed. Recursive execution, validated constrained-source cohorts, remaining
runtime source censuses and named metrics are still current P08 work. Full
registry/F13/global integration baselines remain open. Continue autonomously.
