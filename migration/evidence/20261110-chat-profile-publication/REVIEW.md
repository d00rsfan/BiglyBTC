# Publish chats after the profile epoch

The first user preview could show an empty room list and blank chat status even
with original saved favourites. SessionBridge queued its chat service before
ready() queued the profile identity. The GUI's correct stale-session guard then
discarded this valid service. Publication now follows the identity event on the
same GUI receiver. Shutdown and epoch checks remain in place.

A full generated normal-profile Host + LibraryWindow regression verifies that a
saved history room appears after open and is cleared after close. This exercises
the real service/broker/controller path, unlike the earlier standalone page
checks. The test disables Public Azureus DHT and does not join/post to a room.
Host and page suites passed in 3.00 s. Community also offers original standard
BiglyBT General: Help and Beta: Chat keys; menu tests verify the emitted join key
and absence of implicit sends. The user's screenshot and room data are excluded.

A separate focused optimisation limits request-time tracker/scrape metadata reads
to the current torrent. It retains fresh permission checks, full saved inventory
duplicate validation and final inventory comparison. Full periodic enumeration
is unchanged. A generated regression establishes identical authority scope for
the requested row, successful validation when an unrelated state is unavailable,
continued full-enumeration refusal for that missing state, immediate revocation
when the requested row's DHT permission changes, and refusal of duplicate saved
identities outside the requested subset. That case and the existing saved-source
permission suite passed (77 ms including Qt setup/cleanup). The owner propagates
the hash through HTTP, UDP and Mainline dispatch/completion validation.

A short local CPU sample showed bencode parsing, Node copying and allocation in
the busy session worker, including the saved-target reader. This is evidence for
removing the full-state scan on single requests. It is not a measurement of the
new build's CPU or payload throughput. No whole performance fix is claimed.

The user still reports delayed unwanted seed checks after clean Java exits.
Comparison with an older valid snapshot found previously DONE V1 maps marked
RECHECK_REQUIRED. That snapshot does not establish the immediately preceding
admission state; do not attribute the exact trigger to V2 or an unclean exit.
Import diagnostics now distinguish missing dictionaries/flags, geometry mismatch,
STARTED/RECHECK states and partial-block map errors. SDK fast-resume rejection is
surfaced as a fixed category plus numeric error code, without names/paths/URLs.
A local-only, bounded next-start summary records classifications for the reported
examples before Native exec, without copying the profile or opening payloads.
The [resume task](../../tasks/P06-RESUME-RETIREMENT-ROUNDTRIP.md) remains open.

The separate frozen chat executable was replaced atomically. Existing user
processes and the protected daily-use cache/provider were not changed or stopped.
Exact launcher inputs passed; unchanged SDK runtime controls retain the previous
qualification. A restart of chat-preview is required to load the fix. The normal
build uses GCC 16/Qt 6.12; no fresh sanitizer or real-room post is claimed.
The status-bar unread-chat icon remains an explicit follow-up in the chat task.
