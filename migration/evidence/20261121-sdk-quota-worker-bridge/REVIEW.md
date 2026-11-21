# Quota origin and separate SDK control workers

Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Native implementation `9a0b6f9b71ef518042182a18581e8f1449c1fe9d`, root tree `7c7534deb6b376574b76079bfc59012aef4797c2`.
SDK-wrapper baseline `5580cd876a6849ea0ef9afb99de4457c7f145404`;
common-profile prerequisite `67ebaf9a816cd2857314b986acb397e46059b6f4`.

The quota origin retains exclusive worker ownership of acquire, rate mutation
and explicit close. A SDK class may now consume that same opaque budget from a
different Native control worker. It privately observes the live budget; public
budget current/limits/control methods retain their original worker checks.
Each SDK class still requires its own creating worker for class operations.

A remote class can acknowledge only the exact limits already published by the
origin. A mismatching proposal refuses before changing quota, revoking the old
class epoch or poisoning a usable class. The origin may still change shared
limits through its own SDK class, preserving the existing API behavior. A real
origin change invalidates every earlier passive shared-budget witness before a
remote class acknowledges its new local settings. Exact no-ops preserve epochs.
No profile lease, filesystem callback or saved-source writer permission is added
to a quota/budget/witness. SDK network waits remain outside quota/source locks.

The pinned original 70-assertion/five-class shared-tag reference is inherited
from the wrapper baseline,not executed again. This checkpoint changes the Native
control representation,not original PEPeer/transfer timing or protocol source.
The candidate SDK remains the same isolated one-image artifact SHA-256
`05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e`,
class ABI `0x4242435300020002`,shared quota ABI `0x4242515300010001`.
SDK upstream `740a0b9aeabe00e762cc0efe4a0f27593db2550b` attribution remains the
inherited matched archive/source inventory,not a fresh Git-tree comparison.
No SDK vendor source,header,export or runtime manifest is changed here.

A real old-wrapper control reproduces refusal to bind the origin's budget from
another control worker. The new fixture uses a joined C++ control thread and
actual SDK sessions. Twenty-four assertions cover origin and remote class
readiness,wrong class worker,foreign UID,forbidden direct quota control and
mismatching proposal,passive cross-worker invalidation,exact acknowledgement and
no-op,refused foreign close,origin close and class retirement on both workers.
The existing 31-assertion same-worker wrapper fixture checks preserved behavior.

Actual loopback upload/download use two SDK sessions with distinct Native control
workers and one common tag budget. Both progress under one 16 KiB/s aggregate.
Origin Disabled stops new payload in both sessions before the remote Native
worker acknowledges local settings;protocol keepalives still pass. Ordered wire
markers drain old queued TCP payload before the no-new-data assertion. Remote
live detach releases only that member;its sibling stays prohibited. Restoration,
reattachment,passive origin close and joined worker/session cleanup are checked.

GCC16,scoped ASan/UBSan and Clang24 compile the Native wrapper/fixture TUs;the SDK
itself remains GCC Release. Clang uses GCC16's libstdc++. No whole-SDK,whole-app
sanitizer,TSan or original network timing parity is claimed. The final ordinary
SDK/application checks use the unchanged selected ABI1 image. The optional
availability fixture explicitly verifies Unsupported before session use when
that image lacks shared quota support. Conditional availability is not remote
SDK-worker transport qualification on ABI1.

Header documentation was moved beside the changed quota API after the first
runtime round;the recorded non-comment header lines are identical. The public
SDK-group header stays unchanged. Final builds/tests bind the recorded final
inputs. No unrelated source reformat or warning suppression is introduced.

This lower-level bridge does not make ProfileTagRates itself cross-thread safe.
Normal Host provider census,private source/pre-storage preparation and same-image
Public/Private/Mixed role admission remain required. The private active-rate
fence stays enabled. Global/LAN ownership remains separate unfinished work.
Current SDK selection,delivered daily,launchers,running client and real profile/
payload are unchanged. Component-wide integration baselines remain null.
Continue the normal all-role/source integration without a stop at this milestone.

Review performed by the implementing agent;no separate reviewer/agent was used.

Final results:23 runtime groups passed. Each GCC/scoped SAN/Clang lane passed24
new control assertions,31 preserved wrapper assertions and actual up/down.
The unchanged selected SDK passed all10 ordinary application groups,including
77 Qt rows without failures or skips and explicit optional-API refusal.
The normal application/affected targets rebuilt in108.76s. Exact input/log
identities,scope limits and prior-wrapper baseline refusal are retained.
