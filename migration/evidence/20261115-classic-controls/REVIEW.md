# Classic controls and original tag labels

Implementation `8cbc94b7057d64526fc83e79c4ff635c3cb23198`, repository tree `73306fff3dc161ad6441124201ca1c167a9c317e`; qualified2026-11-15.
This follow-up addresses user-reported daily UI behaviour while the next DHT
chapter was in progress. It does not replace the immutable delivered daily.

A visible enabled command could be dropped when a periodic table snapshot queued
an action repaint just before the click. Dispatch now reconciles current
eligibility while retaining selected identities, profile session and reentrancy
checks. The right menu has one set of torrent actions; toolbar Start/Stop and
Pause/Resume icons follow the current action.

Repeated full-library dictionary/identity audits occurred in per-download chat
metadata reads. An OFD-owned index retains the exact immutable source tree and
validates it once. Copy-on-write edits, a changed freshly loaded tree, duplicate
identities and malformed rows cannot inherit the old validation. Actual file
reads, source ownership and CAS requirements remain. A foreground user control
has priority over the next background DHT/profile scan; current permits complete
on their owner and the background priority wait supports cancellation. This is a
native integration optimisation, not a claim of Java performance parity.

Original `TagBase.getTagName(true)` and built-in `MessagesBundle` labels inform
the display fix: known original tag name/type keys render through Qt translation
in chips, Statistics, editor/menu labels and the Tags column. Raw names, UID,
membership and editor commands are preserved. The original imported updates label
is `BiglyBT Updates`; general language-pack coverage remains F17 work.

Acceptance:31 local-rating QTests passed, including500 immutable index hits,
changed/duplicate-tree refusal, user-control precedence and cancellable wait.
Thirty-two unchanged action-menu tests passed; the remaining legacy duplicate-menu
expectation was updated and passed separately. Eight tag/data QTests passed,
including the new label/identity assertion and the existing original comparator
fixture. Eleven broker QTests passed. The actual Window → Bridge → engine forced
seed regression passes Pause, Resume and Stop, clears saved force intent on Stop
and preserves payload bytes; the existing passive-snapshot busy-ownership negative
case passes too. Totals include fixture lifecycle methods and are in
[the shared receipt](../20261115-shared-azureus-diversification/qualification.json).

User diagnostics and all real-profile/log captures remain private and unpublished.
No real-profile latency/steady CPU acceptance is claimed: a later requested image
must be used for that observation. No backup, process restart or new daily image
was performed. Browser/player and classic Friends priorities are unchanged.
