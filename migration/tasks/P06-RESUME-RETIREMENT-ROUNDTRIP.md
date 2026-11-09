# Existing-library resume retirement and Java handoff

Status: scoped fix tested; fresh real-library acceptance pending.

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
