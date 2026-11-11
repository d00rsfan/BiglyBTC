# Existing-library resume retirement and Java handoff

Status: previous scoped fix tested; real-library acceptance failed, investigation active.

Preserve checked parts when an integrity check or a paused admission is retired.
Retain unchanged original known-empty pieces and partial blocks as well. Existing
maps must never be replaced with invented completion. See the
[implementation evidence](../evidence/20261109-resume-retirement/REVIEW.md).

Next acceptance: after Java finishes its current recovery checks and exits,
launch the corrected Native build on the shared profile. Record resume-decision
categories, confirm completed V1 seeds do not acquire new full checks, then
verify clean Exit / Java reopening. Do not run both clients on this profile.
Pure-V2/nonlinear and mixed RECHECK translation remain separately incomplete.
The startup/admission CPU regression remains open. General-room implementation
continues after this immediate compatibility blocker has been assessed.

## 2026-11-10 real-profile acceptance failed

The user reports delayed growing seed checks after a clean Java Exit; reopening
Java first had traffic and no visible checks. The previous scoped retirement
repair has not resolved the usage blocker. Read-only comparison with an older
known-good snapshot found all-DONE V1 maps now marked RECHECK_REQUIRED. That old
snapshot does not establish the exact map immediately before the latest Native
admission, so it cannot identify the invalidation stage by itself. Do not infer
V2, payload changes or an unclean Java Exit as the cause. Preserve user data and
collect a bounded pre-start summary plus specific import/SDK rejection reasons.

A short live CPU sample identified bencode parsing/Node copying in the session
worker, distinct from file hashing. Per-request tracker authority currently
enumerates unrelated saved torrent state. Restrict metadata reads to the request
hash while retaining current permissions, complete inventory duplicate/CAS checks
and full periodic enumeration. This scoped optimisation is not proof that the
whole CPU/zero-transfer regression is solved; real-profile performance remains
pending. Do not interrupt or restart the user's clients during investigation.

Latest observation: after interrupted Java recovery, Native leaves torrents needing recheck in Waiting, with no visible active checks and a busy CPU thread. This remains critical; no torrent engine change was made in the user-selected chat-identity round.

## 2026-11-10 restored-library startup and metadata work

Candidate e735c85585312101d2afa1da2a6e624f2040440b bounds periodic metadata work, shares immutable snapshots, validates changed inputs, keeps request/rearm checks uncached, interleaves incomplete and ready-seed initialisation, removes the accidental global256-response limit, and adds tracker-error tooltips. [Evidence](../evidence/20261110-startup-metadata/REVIEW.md). The proposed global startup defer was removed after comparison with original Java and user feedback. No wait for Checking or the entire startup queue gates ready seeds/trackers. Six owned transfer scenarios and focused regressions passed across the recorded runs; real-profile CPU/checking/seeding acceptance remains pending. No completion was restored from old backups.

## 2026-11-11 network / resume diagnostic candidate

Native 18ece176d8d36e6a35b284cc5511b7528d94ea35; tree 89d39c5b984efcb89881737eb5f44d17113100c4. [Evidence](../evidence/20261111-network-resume-diagnostics/REVIEW.md) records the Java valid0/DND resume repair, shared-chat Java-mode compatibility, route-aware tracker status and successful-alert subscription, and the requested stdout admission/listener/URL/HTTP400 diagnostics. The separately pinned Public-only SDK derivative retains a bounded400 body without adding network authority. Generated cold/warm/changed-payload, owned dual-stack transfer/chat and exact-image controls passed. Fully completed real-seed Checking, actual HTTP400 cause, real-room connectivity and startup CPU acceptance remain open; do not generalise the DND repair to those reports. No real profile/payload was written by the tools and no completion was copied from old backups. Superseded build products were deleted; keep the current working build and source/evidence, not rollback application copies. No new chapter started.

## 2026-11-11 saved Resume preservation and Public chat acceptance

Native 2171024aa96b0824cac1270b6ba094c0d4edeae4; tree 436e4348b7da099477daa53df5d308de5bef60e7. [Evidence](../evidence/20261111-resume-preservation-chat-notifications/REVIEW.md) qualifies skipped-Compact and pure-V2 cold/warm Resume, actual V2 upload, refusal before unsupported clean-map invalidation, one-pass cache hydration preserving Resume/mtime, and Public chat unread/reading controls. Generated regressions and one V1 tracker/DHT transfer passed. Actual receive-only Public room reception passed independently over IPv4/IPv6, and the user confirmed an outgoing message and reply in the prior preview. Previously accumulated RECHECK work is being repaired in Java; the final preview still requires repaired-library startup/CPU acceptance. Unsupported clean maps are deferred, not silently invalidated. No real profile was launched or written by these tools. Whole client/plugin parity remains open; no new chapter started.
