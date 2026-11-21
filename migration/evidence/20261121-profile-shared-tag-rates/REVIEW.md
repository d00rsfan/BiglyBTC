# Common saved-tag profile owner and complete SDK readiness

Original baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Native implementation `67ebaf9a816cd2857314b986acb397e46059b6f4`, root tree `6636785860e5494ccc852a131ef567ab9ae0f92b`.

## Behavior and source basis

ProfileTagRates now keeps one saved-source capture and one opaque budget per tag
UID across a bounded cohort of SDK sessions controlled by its creating Native
worker. SDK classes and torrent bindings remain session specific. An explicit
session attachment validates the actual torrent handle through that SDK session;
the legacy implicit attachment refuses an ambiguous cohort. Source gates end
before SDK waits or quota control. The ordinary ABI1 SDK continues to admit only
one session and refuses a second before changing the existing source proof.

The selected original TagDownloadWithState/DownloadManagerPeerListener/
LimitedRateGroup baseline and actual 70-assertion/five-class reference are
inherited from `5580cd876a6849ea0ef9afb99de4457c7f145404`. Those actual pinned methods
share group objects across two manager facades and existing/late peer callbacks.
The new Native session/source representation is tested separately here. That
reference was not re-executed for this checkpoint; its constructor/facade and
unexecuted original transport limits still apply. No original timed throughput,
LAN-policy or all-role transport parity is inferred from it.

## Source, lifetime and recovery

A prepared source records passive exact-limit witnesses for every admitted SDK
session. Adding, releasing or reaping a session replaces the weak source-owner
identity; an old proof cannot silently describe a smaller cohort. Any session's
SDK abort revokes its witness. Exact source/SDK no-ops preserve the proof.
Current store lineage, profile ownership, snapshot generation/preimage, worker
and assignment-session checks remain mandatory. Generic writers stay strict;
this read-only preparation grants no standalone assignment permission.

A source failure revokes preparations and holds all admitted SDK sessions.
Recovery resumes only pauses introduced by this owner, preserving a session
that was already paused. An explicit session release pauses that departing SDK
before retiring its classes; sibling budgets and sessions remain owned.
Closing/destructing the profile owner holds its sessions and closes the common
quota before dropping class/budget references. The passive proofs and allocator
retain no profile lease; reopening the generated profile while closed objects
and SDK sessions still exist is checked.

At most eight SDK sessions are admitted before changing the cohort. Cached UID
cells are bounded by the allocator's 4096-cell policy and remain until scope
close. Removed tag budgets become unlimited but retain identity: immediate
re-add must not conflict with an old SDK request still borrowing the same UID.
SDK requests/classes retain their own quota references through retirement.

## Qualification model and limits

The candidate SDK stays isolated and unselected. Its exact artifact SHA-256 is
`05ce92d16e68c4c2f7046ff615dc762bef6d3d012b896bb1915025284ce62f3e`,
manifest SHA-256 `f5490f29a74c6b63bc4f66e6c9dabb6fb2d4cfc6b267dccd7d1cc01d5c249081`,
class ABI `0x4242435300020002`. SDK upstream
`740a0b9aeabe00e762cc0efe4a0f27593db2550b` attribution is inherited from the matched
parent archive/inventory, not a fresh Git-tree comparison. The parent backend
review contains its 1102 source/290 installed header/59 export qualification.
All sessions here use that one SDK image and one C++ runtime.

Generated profile/source tests cover cohort changes and the eight/ninth-owner
boundary, exact no-ops, all-member SDK abort, damaged-source hold/recovery,
limits -1/1/32768, foreign profile/store/worker and close/lease release.
The pre-existing prepared-source controls exercise strict generic-writer
refusal, reload, SDK abort, stale/closed store and retained-proof lease release.
Ordinary ABI1 runs exercise their applicable single-session/refusal branches;
conditional shared-only test bodies there are not new SDK-cohort coverage.

Real loopback upload/download each use two actual SDK sessions, one saved tag,
one actual ManualTagStore/ProfileTagRates owner and paused handle attachment.
Both sessions progress under one 16 KiB/s aggregate. The final payload fixture
also injects a missing class at the second paused attachment. The retained old
implementation falsely returned success from synchronize without retrying that
attachment; a real transfer escaped the common budget. Failed preparations and
attachments now revoke source readiness, hold the cohort and mark pending
bindings for synchronization before recovery resumes any owned pause. The
unchanged failing fixture passes after this correction. Actual source changes apply
Disabled across both, preserve protocol traffic, detach one member without
lifting its sibling's prohibition, and remove/re-add a UID safely. An ordered
wire marker drains previously queued TCP payload before the no-new-data check.
The fixture directly uses SDK sessions and actual production profile/source
classes; it does not represent a full Host/private transport integration test.

GCC, scoped ASan/UBSan and Clang builds compile the changed profile/preparation,
SDK wrapper and fixture TUs. The reused exact Native storage/core archives and
full SDK are GCC Release and uninstrumented. No whole-application/SDK sanitizer
claim is made. Clang selects GCC16's libstdc++; ordinary ABI1 uses its matching
headers and image. Application/current-source consumer results are recorded in
the final qualification alongside exact recipes, input and log digests.

## Continuation

Normal Host/PersistentSession still owns a single ProfileTagRates instance per
engine. This checkpoint makes that production class ready for a common worker
cohort; it does not wire all normal provider roles into one owner. Private I2P
controls live on a separate Qt thread, so the worker-affine profile object must
not be passed across that boundary. A reviewed per-worker SDK attachment bridge,
complete Host role/source census and same-image role admission remain required
before lifting the private pre-storage active-rate fence. Global/LAN policy and
the existing static cross-session rate split remain separate unfinished work.
Component-wide integrated baselines stay null/in progress. The delivered daily,
selected SDK, launchers, running client, real profile and payloads are unchanged.
Continue those steps without stopping at this checkpoint.

Review performed by the implementing agent;no separate agent/reviewer was used.

Final qualification:24 positive runtime groups,72 Qt rows in four isolated
SDK/compiler lanes and119 Qt rows in the ordinary Native consumers,all without
failures or skips. The normal application/affected targets built successfully
in853.08s on the unchanged current SDK. Final probes were relinked against the
exact recorded Native storage/core archives after that build,then all14 probe
groups were executed again. The unchanged partial-attachment reproduction also
passes with those final dependencies. Earlier missing Boost-path,unused helper
and incomplete static-link recipes remain failed build attempts,not acceptance.
An inherited CategoryCommit/std::expected warning in the full application follows
the unchanged application warning policy;no diagnostic suppression was added.

A local evidence-logs attribute preserves exact compiler diagnostic bytes,
including inherited trailing spaces;production whitespace checks stay enabled.
