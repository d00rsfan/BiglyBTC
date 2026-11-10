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
