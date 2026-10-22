# BiglyBTC native desktop preview

This additive `preview/` tree contains the C++23/Qt6.12 desktop preview, its
bounded native core/profile/engine adapters and selected native unit fixtures.
The repository root continues to build its existing plugin SDK by default.
`SOURCE_PROVENANCE.json` describes that older export; the separate
`PREVIEW_PROVENANCE.json` records the first export snapshot and its exact files.
Later development is recorded in Git history and the repository's `milestones/`
records. [Classic navigation source contract](docs/classic-navigation-source.json)
pins the original menu, naming and shortcut behavior.
Read its [closeability correction](docs/classic-closeability-correction.json):
classic main TabbedEntry overrides the factory closeable flag.

The application is under development. Its default preview launcher creates four
synthetic torrents and saved hosting/sharing examples in a fresh private `/tmp/biglybtc-preview-*` profile and passes
port0, which disables transfers. It never opens the user's `~/.biglybt` profile.
Original-profile switching, full networking/session/plugin behavior and complete
UI parity remain unfinished. This milestone adds the first classic shell: a
My Torrents tab, incomplete/complete tables, name filtering and shared General/Files
details. **View > My Torrents (Ctrl+1)** returns to the existing torrent screen
from anywhere in the window, preserving the filter and selection. **My Tracker
(Ctrl+2)** and **My Shares (Ctrl+3)** now open read-only saved-data tabs. They
show original-format saved configuration and recovery-source provenance without
starting services, scanning shares or opening saved paths. All three views can close and reopen with **File > Close Tab (Ctrl+W)** and
the View shortcuts. Closing My Torrents retains its screen state for Ctrl+1. Each torrent
pane has its own column chooser: right-click the header, or use **View >
Incomplete Columns / Complete Columns**. Reset Columns restores the preview
defaults. **View > Save current column layout** explicitly saves both panes
when an unchanged valid primary `tables.config` is available. Full original
saved-column settings compatibility remains unfinished. The **Saved queue
position** column shows the raw recorded position when present. It does not
represent an active native scheduler; unknown values stay blank (—). See its
[source contract](docs/saved-queue-position-source.json). Other
unavailable actions remain disabled. See the milestone verification
record and Actions run for the checks performed on the actual revision.

Build independently with the pinned compiler, Qt6.12.0 and libtorrent2.0.12:

```sh
cmake -S preview/native -B build/desktop -G Ninja \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-16 -DCMAKE_BUILD_TYPE=Release \
  -DBIGLYBTC_LOCAL_ENGINE=ON -DBIGLYBTC_DESKTOP=ON \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.12.0/gcc_64;/path/to/libtorrent-prefix"
nice -n 5 cmake --build build/desktop --parallel 2
ctest --test-dir build/desktop -L unit \
  --no-tests=error --output-on-failure
```

Choose a fresh build path. The toolchain guard requires its exact qualified
GCC16/Clang24 and libstdc++16 identities; a different compiler must not silently
substitute. Local dependency prefixes are examples, not bundled SDKs. See
`native/packaging/README.md` for experimental private-Qt DEB options. The manual
hosted development DEB workflow provisions a recorded Ubuntu26.04 environment;
its presence does not prove that CI has run or that a release was published.

Only the selected native units/fixtures and separately recorded Qt checks can be
claimed passing. The original Java reference build, harnesses, normal profile
and operational evidence are intentionally absent. Some integration wrappers
return skip when these inputs are missing; others require unavailable inputs.
Do not interpret a full unfiltered CTest invocation as supported integration
qualification, and never manufacture Java evidence to make it pass.

Only the already published rating data/local-policy module is included because
the current native target graph builds it. It is not a running rating plugin.
SpeedScheduler and other plugin implementations, unrelated native services,
private evidence, keys, binaries and development builds are excluded.

For an already staged runnable folder use its top-level `./BiglyBTC-preview`
launcher; the installed-layout shell wrappers are not portable folder entrypoints.
To stage a folder from a build configured with `BIGLYBTC_DEB=ON` and
`BIGLYBTC_BUNDLE_QT=ON`, run `python3 preview/migration/tools/stage_preview.py
--build build --output dist --libtorrent-package /path/to/libtorrent-rasterbar2.0t64_2.0.12-1_amd64.deb`
on one line. Enabling the install layout does not build a `.deb`; no CPack or
system installation is needed. The ordinary libtorrent runtime is extracted
alongside the private Qt files for a user-local preview.
The bounded core now also projects original-format `tracker.config` and
`sharing.config` documents, with separate read-only adapters over a held profile
lease. It preserves unknown raw data, reports missing/invalid/recovery sources,
and never initializes hosting/sharing services or follows saved paths. These
primitives and their tab/worker integration are tested with synthetic public
inputs. Full hosting/sharing services remain unfinished. See [source and policy contract](docs/saved-hosting-sharing-source.json)
and `migration/fixtures/saved-services/README.md` within this preview tree.

The cache reader also retains fixed saved DND facts and exposes a pure selected-size,
remaining and permille projection. Missing or inconsistent counters stay unknown;
source defaults and cached completion candidates remain separate. Three optional
columns show **Saved selected size**, **Saved selected remaining** and **Saved
selected done**. Enable them through either pane’s column chooser; they start
hidden, and Reset Columns hides them again. They reflect the current saved
snapshot and stay separate from live progress and completion-pane grouping.
Unknown values use an em dash. Independent synthetic fixtures and pinned source
reviews qualify this bounded primitive, with no Java manager/runtime equality
claim. See the [column contract](docs/saved-selected-columns-contract.json),
[source contract](docs/saved-dnd-progress-contract.json) and
[math/default review](docs/saved-dnd-math-review.json).

The core also reads original classic `tables.config` documents into an owned
raw snapshot and typed requested column facts. It preserves unknown plugin
columns and extra settings, distinguishes exact layouts from user defaults,
and retains original ordered sort IDs without applying them. A separate optional
profile reader loads only `tables.config` and its recovery candidates under a
held lease, without rewriting files. It reports recovery provenance and supports
cancellation between reader calls. The desktop imports a reviewed subset once
when a profile opens. View > Reload Saved Column Layout explicitly imports it
again; ordinary Refresh preserves your session choices. Changes made after a
read request win independently for each pane, and Reset restores the preview's
construction defaults. A valid BiglyBTC preference map takes precedence over this
original-layout fallback. **View > Save current column layout** writes both panes’
13 native column choices into `BiglyBTC.ClassicColumns.v1` in `tables.config`.
It preserves original Java table/plugin records and checks the primary file against
the last successful layout read before saving. Edits made during a save stay local
and unsaved. There is no automatic save on resize, Reset or window close.
Name keeps its responsive width. If all three `tables.config` candidates are
absent, explicit Save creates the first layout with a no-overwrite publication.
Recovery-derived loads, unknown private versions and changed baselines require
resolution before saving. Java’s Reset All Tables command erases the private map; ordinary pinned Java
map saves retain it semantically. See the [native preference contract](docs/classic-preferences-contract.json).
The default sample launcher still creates a fresh temporary profile on every launch. This maps layout IDs, without claiming Java's
registered defaults, plugin columns, full settings or row-sort equivalence. See the
[UI import policy](docs/classic-column-layout-import-contract.json),
[layout contract](docs/classic-column-layout-contract.json),
[profile reader contract](docs/classic-column-layout-reader-contract.json) and
[pinned source audit](docs/classic-column-persistence-source.json).

**Tools > Statistics (Ctrl+5)** opens one reusable, closeable Statistics tab.
Ctrl+1 restores My Torrents with its selection, filter, columns and details.
Activity defaults to **Session traffic**, retaining up to600 actual combined,
payload and protocol rate observations across transfers and closing/reopening
Statistics. Payload and Engine traffic retain their separate per-operation views.
Unknown intervals break graph lines; hidden/minimized views retain data without
painting or starting an engine.

Transfers > Current separates the persistent native session, latest operation,
live overall accounting and last-saved totals. Raw interval rates remain separate
from ten-second windows and thirty-second smoothing over actual sample arrivals.
The window arithmetic follows the original source; sample arrival timing does not
claim equivalence to the original Java packet-event timing.

A source-qualified persistent session enables compatible **BiglyBT.statistics**
accounting and **Set mark / Clear mark**. Marks save immediately; original overall
arithmetic updates every minute and saves every ten updates and on explicit close.
Unknown document fields survive saves. Failed or recovered storage remains visible
and blocks further writes; transfer success and lease release remain independent.
Only actual joined close receipts can certify final peer counters. The ordinary
unbound engine fallback and offline sample remain read-only.

Transfers > History retains its saved UTC day/week/month/rolling browsers. Qualified
sessions append original-format six-field minute records under stats/YYYY/MM/DD.dat,
including start/end and UTC rollover. DHT is a separate domain and is explicitly
disabled in these isolated local sessions. Unknown intervals are not filled with
zero; ambiguous append failures stop automatic recording.

**Cache**, **Tracker**, **Counters** and Transfers > **Connections** show actual
native engine counters and gauges, with name filtering and numeric sorting.
They consume the existing stats responses and never start a service on opening.
Values are labelled unavailable before observations and last-observed after close.
These native metric tables do not claim the original Java cache/tracker service UI
or the remaining DHT, DDB, Vivaldi and Country providers are complete.
The default generated offline sample has no engine observations.

Statistics > Transfers > History now browses one saved UTC day. Select a date or
use Previous day / Next day, then Refresh history (or F5 while History is selected)
to reread it. The table shows all 24 UTC hours with separate payload, protocol and
DHT upload/download bytes. An em dash means no saved observation; an observed zero
is displayed as 0 bytes. Signed adjustments remain signed. The daily payload
summary is separate from saved lifetime totals and live engine graphs.

History reads original stats/YYYY/MM/DD.dat v1 records under the held profile,
without modifying records or caches. It loads only when selected and uses the
existing worker queue. Changing profiles clears old results; closing/reopening
Statistics rereads the selected/default day. The offline launcher includes
explicitly generated saved history for previewing this screen. Rolling queries are available in the separate Rolling history tab described
below. Qualified native sessions also record history as described above. Format, bounds and the independent Java reader fixtures are documented
in docs/saved-history-contract.json and native/tests/fixtures/saved-history/.

The Statistics > Transfers > History screen now also offers **7 days** beside
Daily. Select an ending UTC date to read seven independent daily files, oldest
first, through the same profile lease. Missing files, present empty files,
observed zero and signed counters remain distinct. Previous/next move seven days;
Refresh history or F5 reloads only this overview. No weekly total is inferred:
the original whole-period query can account for midnight spill differently from
independent daily reads. Use Rolling history for whole-period queries. Live
history recording remains open.
The offline launcher generates sample periods for today and 2024-01-02 without
opening the original profile or enabling transfers.


Saved History charts show payload downloaded and uploaded above both the Daily
hourly table and the 7 days table. Each point uses its exact signed saved value;
unknown hours or days leave gaps, while recorded zero remains a point. Hover a
bucket for its UTC label and exact byte values. The existing date navigation and
Refresh/F5 update the selected chart and table together. These fixed saved UTC
charts do not add rolling queries, history recording or a combined weekly total.
See [chart contract](docs/history-charts-contract.json).


In Statistics (Ctrl+5), open Transfers → History → 7 days and choose Graph
resolution: Hourly values for 168 UTC hourly buckets across the selected seven
dates. Daily totals remains the default and the seven-row daily table stays
visible in both modes. Switching resolution uses the already loaded records and
does not reread files. Hover a bucket for the full UTC date, hour and signed byte
values. Missing observations leave gaps; recorded zero stays visible.
These selected UTC dates differ from the original rolling current-clock query.
See [hourly resolution contract](docs/history-hourly-contract.json).


Statistics remembers its selected subviews, both saved History dates and graph
resolution when you close its tab (Ctrl+W) and reopen it (Ctrl+5) in the same
profile session. Reopening creates fresh widgets and reloads saved records through
the existing worker. Only scalar choices are retained in memory; no data snapshots
or widgets are kept. A new profile session clears closed-tab navigation memory.
Existing visible-page session reset behavior is unchanged. This preference does
not survive restarting the app and writes no original configuration files.
See [navigation contract](docs/statistics-navigation-contract.json).


Transfers → History → **30 days** displays thirty independent saved UTC daily
records ending on the selected date. Previous/next move exactly thirty days;
Refresh 30 days or F5 rereads this range through the same held profile. The table
keeps all six signed counters, distinguishing missing files, present files with
no observations, and recorded zero. No 30-day sum or varying calendar month is
inferred. The chart switches among Payload, Protocol and DHT using the accepted
records without another read; keyboard inspection and Copy selected values work
on all thirty date buckets. Copy writes only when explicitly invoked.

Closing and reopening Statistics in the same profile session retains the date
and traffic choice as scalars, creates fresh widgets and rereads saved records.
This read-only browser adds no recording, cache writes or persistent preferences.
Its selected daily records differ from the original rolling month graph's hourly
queries and pixel merging; it does not establish Java monthly graph equality.


**Transfers → History → Rolling history** offers Hour, Day, Week, Month, Year
and All, with previous/next periods and Refresh/F5. It reconstructs original
minute observations and clamps queries to their captured UTC time. Month is a
fixed30-day span with720 hourly buckets; Year uses365 daily buckets. All finds
the earliest saved UTC date through a bounded read-only inventory. Payload,
protocol and DHT remain six separate counters. Independent whole-period totals
can differ from graph bucket sums at boundaries or spilled records, matching the
original query's filename membership rules. Missing/unobserved values remain
unknown, observed zero and signed adjustments remain exact. Hidden views defer
work; stale responses cannot replace newer selections. Reopening retains scalar
period/offset/traffic choices and reads fresh data. No history/cache writer is
started. Application-global recording, lifetime accounting, Java smoothing and
the remaining Statistics service views are still unfinished.

Transfers → Current now shows all six measured engine interval rates: combined,
payload and protocol for receive/send. They retain the existing one-operation
scope and are independent of saved application totals.


**Persistent peer accounting prerequisite (P04-29)** adds a worker-owned engine
that can retain actual peer counters across consecutive experimental single-file
and multifile operations. It records absolute observations independently of
optional UI callbacks, samples an admitted session while idle, and coordinates
normal window close with worker cleanup before releasing the profile lease.
Existing Activity and Transfers widgets continue to show their documented
per-operation measurements; exposing the new observations is a later milestone.

The portable preview and ordinary builds keep the existing per-operation engine
path. Persistent admission is off by default and requires an exact approved
source/build/install manifest plus checks of the actual loaded library. A version
string alone does not enable it. Qualification uses separately owned loopback
fixtures, including a test-only instrumented library which is never shipped.
See [the persistent accounting contract](docs/persistent-peer-accounting.md)
for the counter domain, conditional retirement guarantees and build boundaries.
That earlier P04-29 prerequisite did not write lifetime totals or history; the connected Statistics implementation described above now supplies compatible accounting and recording.

The current Statistics controls also read the original smoothing setting, show
applied session-global rate/connection limits and proxy mode, and refresh the
visible saved History query after successful minute-record writes. Transfers
shows total/since-mark averages and ratios. The isolated native session settings
are distinct from whole original configuration/runtime activation, which remains
in progress.

The connected controls milestone is qualified at source `2c5a81227e750202403fec188fbb71817aa3cfca`: GCC17 affected suites, Clang17, leak-enabled ASan/UBSan17, hosted102, portable smoke and actual private-runtime workers passed. Original Java classes read both actual native-written test profiles. See [validation receipt](../migration/evidence/statistics-controls-20261022.json).

A qualified native owner now starts on successful profile open with an explicit owned loopback endpoint, before a first transfer or opening Statistics. Idle zero traffic and elapsed time are actual engine observations; the first transfer retains that owner. Offline port0 and unqualified fallback remain read-only. Transfers Current adds the original combined-traffic session share ratio. Disk, Tracker and Connections now provide compact named summaries above their searchable raw measurements, with distinct counts and units.

Manual download-tag Statistics reads original `tag.config` on opening **Tags**
or choosing **Refresh tags** (F5). Original names, visibility and canonical torrent
members are retained without rewriting the file. The table joins declared members
to the current saved library and displays actual same-torrent combined and payload
rates when the native owner proves coverage. Missing observations remain unknown;
idle zeros require an actual sampled idle owner. Tags overlap, so their rows are
not session totals. Profile changes and close/reopen reject stale generations.
Automatic tags, rule execution and historical tag graphs remain separate work.
Opening this view never starts a torrent or network service.

Activity > Session traffic defaults to **10-second rates**. The graph selector
also offers measured interval rates and configured smoothed combined rates.
All modes use the same retained observations; changing modes does not request
engine data. Missing source observations leave gaps. Smoothed payload/protocol
components are unavailable because that provider supplies combined values only.
The latest-interval numeric facts remain separately labelled.
