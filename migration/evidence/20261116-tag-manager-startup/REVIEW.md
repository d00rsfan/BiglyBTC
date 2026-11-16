# Original tag-manager startup mode across automatic rules and history

Native source `613074bdb3e969d50ece8464e930cfdeda2b3bcf`, tree `fcedef22197f8b3ad0b03eca6e7e68df7c5aa84d`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Original TagManagerImpl.enabled is a
startup static final setting; ConfigurationManager.getBooleanParameter narrows
through Java int before testing zero. The already qualified native history
reader retained those rules but the automatic constraint worker did not use it.

One immutable profile-bound startup observation now shares the existing guarded
primary/settings-candidate read across history capture and automatic rules. Its
constructor and enabled value are not publicly settable. Ownership comparison
rejects a fact from another profile. The normal Host captures it once on normal
open and passes the same value to later rule workers. A saved config toggle does
not change this captured startup fact. The existing history API remains a
compatible wrapper. The closing Host releases the retained observation before
profile teardown; it supplies no new tag/network/publication rights.

Disabled workers retain definitions and existing memberships, perform no rule
evaluation/publication and collect no new-download work. Thread/store/session/
turn, cancellation and terminal-close checks remain in force. A new logical
startup can capture the current saved mode. Pending or malformed startup settings
cannot initialise an automatic worker; primary settings and exact preimages
remain mandatory. No recovery candidate is replayed or silently repaired.

Actual generated profile tests prove disabled rules leave raw tag bytes unchanged,
a later saved enable toggle cannot reactivate either the existing worker or a
second worker sharing its startup fact, a fresh enabled owner can apply the
ordinary true-rule CAS, foreign profile facts fail, and disabled close remains
terminal. Original int-narrowing examples0,1,2^32,2^32+1 and-1, cancellation and
pending settings are checked through the actual file reader. Existing normal
supported-rule/source/positive-dependency checks pass. The actual normal-window
history-removal row additionally proves the same disabled startup mode remains
fixed despite a current config toggle and preserves prior history tags.

One coherent GCC16/C++23/Qt6.12.0 application link passed before a test-only config
insertion/reload correction. That link receipt is retained without relinking
unchanged application sources; the corrected tests were rebuilt and run. The
initial missing-key setter fixture failure is excluded. No fresh Java runtime
or full original tagger equivalence is claimed: the boolean source routine was
reused without changing its established primary/int/candidate semantics.

No live profile, payload, running client, protected SDK, current daily or launcher
was changed. No preview or rollback image was collected. This completes the
startup-mode dependency of the current registry chapter; actual state/category/
internal/swarm/peer sources, named metrics and recursive constraints remain.
Continue that work autonomously without a chapter boundary pause.
