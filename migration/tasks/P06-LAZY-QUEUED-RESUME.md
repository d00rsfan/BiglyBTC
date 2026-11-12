# Queued original torrents: select before checking and preserve pending progress

Remain on the existing-library Checking blocker. No next chapter. User confirms
original Java leaves pending verification in Queued, checks on Force Start, and
reuses the checked map after stop/queue/another Force Start. A compact private
pre-launch baseline establishes which maps were already invalid. Never put real
profile identities, titles, queue numbers, paths or logs in public evidence.

Generated failures reproduced before this change:

- Ordinary startup initialized every retained Queued admission through the check
  scheduler before original queue selection. An ignored queued seed hashed all
  three pieces despite having no queue permission.
- Retirement of a normal original-library admission with no completed check and
  no imported map replaced historical completedbytes/filedownloaded with zero.
  Both linear and linked seed/partial fixtures reproduced the metadata loss.

Hold ordinary Queued initialization until the existing original queue selects
Run, or explicit Start/Resume/Force Start permits initialization. Use original
saved completion/DND classification only for queue ranking while unchecked.
Neither this classification nor a held queue entry certifies any piece. Keep
actual check scheduling/one-operation-per-filesystem rules and stop_when_ready.
No network payload is released before actual SDK checking settles.

An unclassified retirement with an already-conservative, unimported resume keeps
its historical completion/file progress. Actual traffic/time accounting remains
separate. Completed checks must still replace saved progress; imported resume
proof failure and detected hash failures retain their conservative paths.
Do not perform completion moves or Compact finishing from an unclassified map.
No historical DONE map is restored blindly. External/mixed ownership is unchanged.

Qualify generated negative/positive queue cases, queued joined close/reopen,
Force Start actual hashing, checked warm restart without hashing, linear/linked
seed/partial counters, existing check scheduler and unchanged-payload controls,
V1/V2/Compact restore, profile publication and one actual ordinary transfer.
Changed clean payload remains refused before implicit invalidation as previously
specified; explicit read-only recheck has its existing separate controls.

Original Java baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f:
StartStopRulesDefaultPlugin queued/waiting admission and RDResumeHandler progress
save semantics; existing saved DND projection. SDK source and overlay unchanged
from the preceding resume transaction evidence. Record final implementation and
results in a later provenance commit. Then ask for fresh Java Exit, refresh the
small private baseline and replace the diagnostic preview; do not launch it while
Java is running. Real-profile acceptance remains open until observed.
