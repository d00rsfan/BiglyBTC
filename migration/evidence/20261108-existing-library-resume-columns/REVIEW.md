# Existing-library resume, configured columns and exit confirmation

Status: scoped normal Release qualification passed. No whole component or migration phase is complete.
Original reference: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.

Opening a saved Java library could force full data checks for linked linear
files and V1-selected hybrid torrents despite a valid original checked-piece
map. The mapped disk adapter always returned `need_full_check`. Startup now
admits an authenticated primary map for these linear layouts. The mapped disk
adapter accepts it only while the named files, descriptors, sizes and content
metadata still match the captured proof. It does not use `seed_mode`.

Saved `valid=0`, unknown/RECHECK_REQUIRED pieces, stale or changed files,
recovered backup state, compact layouts and pure-V2 resume translation retain
conservative checks. This does not convert an already invalidated map into
proof of checked data. Explicit Recheck still reads and hashes the payload.
Current hybrid coverage includes single-file and padded multifile metadata.
Preparing resume metadata is distinguished from actual Checking with progress.

The original-column correspondence was incomplete. One shared 33-column
catalogue now covers every canonical visible ID found in the user's two main
pane configurations, including Downloaded, Ratio, Tracker Status, Tags, swarm
counts, dates, category, limits, idle times, merged bytes, file count and comment.
Both panes retain their independent visibility, order, width and sort requests;
Name width and narrow columns are included. Existing private 13/15-column Native
preferences append new columns hidden, preserving the stable ID prefix.

Downloaded/Ratio use good received bytes after discarded/hash-failed bytes.
The completion date uses the original completed-time parameter rather than the
last-complete attribute. Numeric values remain numeric for sorting. Tracker
status uses actual announce/reply/error alerts and does not display raw URLs or
tracker response text. Manual tags are read from the original tag configuration.
Unavailable service values remain visibly unknown, not invented zeros. In
particular chat/subscription provider icons are not implemented by this change;
their configured columns are present. This is column-selection/data integration,
not completion of every original plugin or every advanced column option.

Exit uses an application-modal confirmation when actual integrity checks are
active or original `confirmationOnExit` requests it. No/escape leaves the client
and transfers running. Yes uses the existing joined, checkpointed shutdown.
Repeated close requests do not create competing prompts.

The live profile remained under Java ownership during development and testing.
No backup restoration, automatic profile repair or live-profile runtime test is
claimed. Fixtures contain generated names, paths, tracker endpoints and tags;
no actual torrent names, hashes, paths, credentials or profile files belong here.

CPU investigation is deferred by user instruction until unnecessary Checking is
fixed and the live workload can be assessed again. Subsequent feature priorities
remain Azureus DHT, chat rooms, swarm discovery. Merging is deferred until just
before the embedded browser/media player, which are last. Do not start that
chapter while finishing this bounded repair round.

Implementation: `69a401db0e6c261672ab64cac230c0f04774c8b4`.
Native subtree: `8360cb793fb26d5d6ec9542b23d6b08d73901516`.

Qualification used GCC16 Release/O2, Qt6.12.0 and the existing fixed provider.
The historical cache directory is named `san` but contains a normal build.
No fresh full-sanitizer qualification is claimed for this repair.

| Group | Passing data cases |
| --- | ---: |
| Original resume and per-filesystem checks | 14 |
| Table behaviour, sorting and modal exit | 17 |
| Both original pane layouts, including narrow/hidden widths and reentry | 34 |
| Private preference column behaviour | 25 |
| Save/reload/restart preference integration | 19 |
| Actual Host primary/cache/tag projection | 4 |
| IPv4/IPv6 owned payload and warm restart | 2 |
| Explicit read-only Recheck | 3 |
| Private preference codec | 29 (498 assertions) |

The payload, explicit-Recheck and pure-codec successes are retained from the
preceding batch: subsequent changes affected header initialization, UI labels
and test fixtures. Failed attempts remain failed in local records; the final
layout suite passed after correcting the hidden-width assertion. Compilation
logs were retained locally and only errors/summaries were read.

The existing daily-use command points to the new qualified normal executable.
Close Java normally before using the shared profile; preferably let its current
checks finish so their checked maps can be saved. The new build has not been
opened against the live profile during this round.
