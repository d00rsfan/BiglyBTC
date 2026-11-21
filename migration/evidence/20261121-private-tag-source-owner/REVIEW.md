# Explicit private tag source and SDK-before-storage owner preparation

Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
I2P helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`.
SDK upstream `740a0b9aeabe00e762cc0efe4a0f27593db2550b` remains inherited
matched2.0.12 archive attribution. SDK vendor/source/artifact unchanged.
Native parent `19d9108b08aedb24d0be7728a5db76421486f6ec`;
implementation `01acbfa58e39d592de8222f65f7a36b6d2e95846`,root tree `3c110be4ccdf614477e0c1acd9cb41559399e3c8`.

A separate PrivateTagRateSource captures actual profile ownership,complete
current manual-tag source,canonical/full expected target,actual SDK session,
pending target lifetime and exact readiness across every admitted SDK owner.
It contains weak lifetime witnesses and bytes,no profile lease/source gate.
Matches are passive and can compare a fresh independent source reader. This is
separate from generic PreparedTagRateSource:its actual store lineage/CAS rules
remain unchanged. Foreign proposals refuse before admitting a new cohort.
Unsettled source,changed full identity,SDK/cohort abort,cancel/remove/close and
source replacement revoke or refuse the preparation. ABI1 refuses this new
shared-source preparation cleanly and leaves its ordinary paused target valid.

OwnedProfile::prepareWithTagRates and prepareTorrentWithTagRates explicitly
accept a transient controller reference on the original I/O worker;they never
retain or invoke that reference on the socket turn. They capture the accepted
child's raw full identity under the source gate,release it for SDK class/cohort
preparation,then reacquire/revalidate the exact configuration and child before
ManagedStorage. Only the actual OwnedProfile friend can bind the sealed source
to its own SDK session for storage. A foreign controller refuses before original
resume/inventory/payload writes. The default prepare path still refuses saved
active private tag rates. Tag Manager disabled cannot consume a prepared shared
source;the old unprepared disabled behavior remains unchanged.

Native stop_when_ready is preserved as the actual check-and-pause fence.
The new explicit installForChecking APIs require complete metainfo,non-auto-
managed parameters and stop_when_ready;the ordinary paused API stays strict.
SDK constructor factories attach actual classes before checking/events. Even a
stale callback leaves the SDK's native ready-state pause and zero payload.
No post-add resume can overwrite an already completed ready pause. The private
owner verifies exact binding after add;queue Run additionally needs its current
private source. Prepared member lifetime retains the constructor bundle.

Actual private owner tests wrap the real SDK constructor and use owned generated
profiles/identities. They prove SDK-before-payload and released source gate,
valid shared source acceptance,and default/foreign/held/stale refusal before
storage effects. They do not start I2P transport or claim private payload credit.
GCC16,scoped GCC16 ASan/UBSan and Clang24 compile the modified source/owner/test
TUs against Release SDK/Qt/storage dependencies. The controller and SDK wrapper
have their separately qualified instrumented variants. No whole-SDK SAN/TSan.

Clang initially mixed identical original and overlay relative headers;the
isolated include graph was corrected to consistently use the exact header
snapshot. No product header change was made for that harness failure. The
initial fixture indentation warning was fixed with braces. Both diagnostics are
retained;only final exit0 runs qualify.

This explicit API is NOT yet activated by normal Host startup. Live private
source/stamp refresh,Host all-role assignment effects,actual tagged I2P payload,
pending promotion and common global/LAN owner remain open. Default private
active-rate fence,selected SDK,daily,launchers,real profiles/payloads and Original
HEAD/index remain unchanged. Whole feature/chapter and integration baselines
remain in progress. Continue those remaining paths without a checkpoint stop.

Final qualification:134 unique runtime groups,294 Qt rows,zero failures/skips. Both full application builds pass. Source/controller and actual private owner/storage controls pass per GCC/scoped SAN/Clang;old ABI1 shared-source refusal and existing Native/ordinary behavior pass. No actual tagged private transport or normal Host activation credit.
