# Native torrent properties and vertical resizing

Source: BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. Native resize `b9db322e088d3f8e75b1aec256e077b9e8b401e5`;
native properties `4afe96eca3d08799b65e0ff341df5d2cf5de9862` (tree `5656cbea1aebffaba7f16af680d1aed0ad38b268`).
Reviewed by Codex; no independent reviewer/sub-agent participated.

## Result

The 15 core property views registered by `MyTorrentsView.registerPluginViews`
now have native content: General, Files, Tags, Options, Trackers, Peers, Swarm,
Pieces, Piece Map, Piece Blocks, Activity, Torrent Information, Privacy, Console
and Transfer Statistics. Open them through the existing double-click property
page. Existing file priority/skip/relink/reclaim controls and manual-tag editor
remain the mutation paths. No disabled fake plugin page was added. Plugin-specific
UI parity is separate from these core registrations; existing local rating and
Public Chat Rooms controls remain available.

Statistics previously propagated an inactive page's minimum height to the whole
window (885px reproduced). The workspace now scrolls. Tests exercise active and
inactive Statistics, maximize/fullscreen/restore cycles,550px height and a visible
status bar. Long property forms likewise have scroll containment.

The existing Host worker publishes only the selected visible torrent section,
at most once every two seconds. Session/request/hash/sequence checks reject stale
GUI deliveries; observed histories reset on a new engine epoch/admission. Views
contain owned values only, without raw handles or borrowed SDK block arrays.
Minimizing/hiding suspends interest. No second alert consumer, GUI-thread engine
RPC, library-wide inspection timer, payload read or hashing command is introduced.
Ordinary v1 torrents do not always retain a cached piece vector. The inspector
therefore requests the actual bounded SDK bitfield for visible piece/file views,
without changing the engine's proof or resume state.

Trackers display actual route/protocol results, including separate successful
and failed routes, scrape counts, next-announce delay and detailed status tooltips.
The existing health collector uses protocol keys1/2; the SDK uses enumV1/V2, so the
projection maps them explicitly. Update Tracker/Request Scrape are explicit
commands, revalidated against the retained admission, checked/active state and
existing policy. Opening a tab does not request an announce.

Peers reuse the global peer table for one actual torrent. The swarm drawing shows
actual connections and current payload activity; it does not enable merging.
Original Files/Trackers/Peers/Pieces table settings are projected onto supported
column IDs (visibility, width, position, numeric sort). Unknown original columns
stay untouched in configuration. File completion uses original Java geometry,
not a positional zip with SDK synthetic v2 files, and reports verified progress.

Rate edits (`maxdl`/`maxul`) and connection edits (`attributes.parameters` keys
`max.peers`/`max.uploads`) are separate one-file transactions. They require a
stopped, fully retired/forgotten target, compare the displayed values, use existing
source guards/CAS, and preserve opaque fields and resume/info. Base connection
limits are consumed on the next admission; upload slots follow Java's minimum
of two. This UI never edits configuration behind a retained storage owner.

## Qualification

- Release C++23 build, GCC16, Qt6.12.0; existing qualified SDK unchanged.
-16 UI QTest passes: all15 tabs, stale packet/hash rejection, draft retention,
  file-index/progress/unit projection, existing peer/tracker/status regressions,
  and window shrinking after state changes.
-4 options QTest passes: original rate/parameter persistence, stale/active/invalid
  refusal, exact mixed DONE/RECHECK resume and info preservation, opaque keys and
  generated payload unchanged.
-3 QTest passes in the single owned IPv4 cold/warm tracker/DHT/payload case.
  It now observes a retained seed's actual piece map, original file progress and
  route replies; passive map/file inspections leave its state file byte-identical.
  Existing transfer/counters/restart assertions also pass. No public network or
  real profile was used by these tests.
- Synthetic `torrent-properties.png` was visually inspected. Full build/runtime
  logs remain local; only aggregate results and generated visuals are published.

## Bounds and remaining parity

This completes the core-view implementation chapter, not every advanced Java
column/action or plugin UI. Whole-source components remain in progress until that
broader parity is covered. Supported original settings are restored read-only;
new detail-table layout edits are session-local. Extra original columns, advanced
seeding-specific option overrides, per-peer management actions and additional
privacy tools remain follow-ups. Those saved fields are preserved, not advertised
as editable/applied by the four new base-limit controls. IP-filter stays low
priority; browser/media player are skipped as requested.

The map shows ranges of at most65,536 pieces, with actual bitfield collection
bounded at2,000,000 pieces and remote availability at1,000,000. Larger observations
are explicitly marked bounded/unknown. Active pieces cap4,096, blocks16,384,
peers20,000 (peer bitfield allocation budget32MiB), swarm drawing250. Console
retains512 actual selected-torrent engine events, up to4KiB each, while observed.
Activity retains at most900 real samples/30minutes in memory; it does not invent
history from before observation. Full hash identities are shown from the qualified
retained owner; saved-only views retain the original selected identity and leave
uncertified full hashes unavailable. Current-owner counters remain unavailable
when no owner exists. Real Wayland visual acceptance and library behaviour remain
user observations, not claimed by offscreen/owned tests.

Candidate SHA256 `49d12e482ca48333179d1183d31c08fffc326d5c69b5ebe6d5755eaf8a083812` (19598616bytes). The currently running
frozen preview was neither interrupted nor replaced. No backup or rollback image
was made. Promotion awaits normal Exit because the launcher holds a shared runtime
lock. Before a new real-profile run, compare compact metadata with the existing
private baseline, including lost DONE bits inside already-pending maps.

Next chapter: swarm merging, ordinary priority after this review. Not started.
