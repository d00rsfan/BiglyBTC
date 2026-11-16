# Current-membership Untagged policy and original listener lifecycle

Native source `c019189ad228a30c1da41224f3d5f275aee70b33`, tree `72b02fa95ff3f02007eaa1927396f1483a05c8c0`; original BiglyBT baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f` remains unchanged.

An isolated JDK17 helper invokes the actual TagPropertyUntaggedHandler.enable,
tagAdded,addDownloads,handleProperty and captured TagListener callbacks. Core/
global managers/tag APIs are supplied inert recording boundaries; actual manager
type lookup/COW tag roster is retained. The type listener add/remove registration
is an inert boundary, while each actual per-tag listener is stored and delivered
on real recording-set changes. No Core/client, network or live profile runs.
Initial enabled/initialised/two-Untagged-rule state is a supplied post-init
boundary; constructor/startup dispatch ordering is not qualified here.

512 cases cover two ordinary/saved-Untagged membership masks,persistent/destroyed
flags and actual add/remove/roundtrip/remove-all/disable/re-enable sequences.
The Native passive policy is compared against the112 persistent,non-destroyed
ordinary lifecycle cases. Other source cases remain evidence, not newly claimed
Native qualification for nonpersistent/destroyed event handling or every callback.

A separate actual lifecycle case demonstrates an original counter error: one
persistent alive download in ordinary tags A and B starts outside two Untagged
tags. Disable both Untagged properties, enable one again, then remove A. Actual
handler output retains B but adds Untagged and loses the count entry. Source
inspection shows disable removes the type listener/clears counts while leaving
per-tag listeners; enable registers another listener on each existing tag. Their
separate added sets can decrement the rebuilt count twice for one removal.
This is an API-level reproduction with inert boundaries, not an SWT/UI or full
client reproduction. No report was sent externally.

The new Native value policy derives other-tag truth from current manual membership
witnesses and explicit Untagged roles. It has no accumulating listener count.
A real known positive survives other unknown/partial entries; unknown roles and
memberships remain unknown. No complete census is inferred from an empty input.
At most4096 entries are accepted; excess input refuses rather than truncating to
negative. Only a separately established complete census can produce known no
other tag. The projected persistent subject may be added only from that known
negative; a known positive may remove it, and unknown data keeps membership.
Nonpersistent subjects do not get an inferred addition.

The source regression explicitly expects a current snapshot containing B to keep
the subject outside Untagged, avoiding the reproduced original bookkeeping bug.
The user's standing instruction that Native should correct verified original
bugs guides this proposed treatment. This is still a passive prerequisite with
no new effect grant or integration baseline advancement; the real source owner
and its negative-census proof must be qualified before activation. Callback and
within-event ordering are not silently declared equivalent to snapshot policy.

GCC16 Release/C++23 core build and current reference/counterexample/unknown/bounds
checks pass. No production consumer changed, so no extra application link or old
green UI/Store matrix was repeated. No fresh sanitizer, SWT, live-profile or full
Untagged service qualification is claimed. Agent self-review only.

The core membership policy still rejects active pp.untagged configuration for
changed membership writes. Actual owner, complete current manual census including
ambiguous/orphan/hybrid identities, source/CAS/journal fences, bounded multi-tag
publication, configuration UI and dynamic service lifecycle remain current work.
No whole registry/F13/global baseline advances. No client,profile,payload,SDK,
daily/launcher or rollback image changed. Continue autonomously.
