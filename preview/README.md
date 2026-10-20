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
New pages start on Activity; Ctrl+1 restores My Torrents with its selection,
filter, columns and details. Activity shows at most600 actual observations of
the latest transfer's payload download/upload rates, with an explicit transfer
name and actual arrival times. It starts empty and resets for another transfer.
Hidden views collect bounded observations without a sampling timer or painting.
Global data-plus-protocol rates remain unavailable.

Transfers > Current shows last-saved overall totals, uptime, separate data,
protocol and DHT counters, mark baselines and derived since-mark values from the
original default `BiglyBT.statistics` filename. Its fixed-leaf read-only adapter
uses the existing profile lease and reports primary/backup/saving provenance.
Absent or invalid values stay unavailable. Combined counters are not added to
their splits or DHT again. Mark time is an epoch-millisecond label, not a save
timestamp. Refresh saved totals/F5 does not refresh the torrent library or write
statistics. Application-wide session counters, global history and the other
original Statistics subviews remain unfinished. The generated launcher sample includes
original-format saved statistics; offline Activity stays empty. See the
[Statistics contract](docs/classic-statistics-contract.json).

No performance improvement or full migration completion is claimed by this source
milestone.

Transfers > Current also shows measured counters for the **latest owned engine
operation** when an experimental transfer runs. It reports combined peer bytes,
payload and derived protocol bytes in each direction, plus interval receive/send
rates. The first observation has no rate. Each operation starts fresh; ended or
cancelled operations retain their last observation, which is not an exhaustive
shutdown accounting. Opening/reopening Statistics restores the latest operation's
facts, and a different profile clears them. The ordinary generated sample has no
active engine, so these fields remain unavailable there.

These measurements belong to one temporary engine session, not the original
application-wide session. IP overhead, tracker and DHT traffic are excluded.
Intervals use monotonic observation times and exact checked integer arithmetic;
they do not reproduce Java's ten-second smoothing. Sampling is optional and cannot
change transfer success. Saved overall totals are read independently and are never
written or incremented with these operation counters. See the [engine statistics
contract](docs/engine-statistics-contract.json). Global Activity/history and a
persistent all-application engine remain unfinished.

**Activity > Engine traffic** plots actual combined, payload and protocol rates for
receive and send from the latest experimental engine operation. The original
Payload view stays first and remains separate. Up to600 known-rate observations
are retained by the Window while Statistics is closed and replayed on reopening.
Plots use the engine's steady elapsed observation times. Skipped observations and
unavailable-rate intervals break the lines; a first unavailable rate is never
shown as zero. Ending an operation retains its last observed history, and a new
operation/profile starts fresh. Refresh saved totals preserves these observations.
Hidden/minimized graphs cache owned data without painting or starting timers.

This is temporary per-operation history; global application traffic, Java rate
smoothing, persistent history and the remaining original Statistics services are
unfinished. The offline launcher generates no engine samples. See the
[engine Activity contract](docs/engine-activity-contract.json).

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
explicitly generated saved history for previewing this screen. Original rolling
history graphs, live history recording and persistent global ownership remain
unfinished. Format, bounds and the independent Java reader fixtures are documented
in docs/saved-history-contract.json and native/tests/fixtures/saved-history/.

The Statistics > Transfers > History screen now also offers **7 days** beside
Daily. Select an ending UTC date to read seven independent daily files, oldest
first, through the same profile lease. Missing files, present empty files,
observed zero and signed counters remain distinct. Previous/next move seven days;
Refresh history or F5 reloads only this overview. No weekly total is inferred:
the original whole-period query can account for midnight spill differently from
independent daily reads. Rolling graphs and live history recording remain open.
The offline launcher generates sample periods for today and 2024-01-02 without
opening the original profile or enabling transfers.


Saved History charts show payload downloaded and uploaded above both the Daily
hourly table and the 7 days table. Each point uses its exact signed saved value;
unknown hours or days leave gaps, while recorded zero remains a point. Hover a
bucket for its UTC label and exact byte values. The existing date navigation and
Refresh/F5 update the selected chart and table together. These fixed saved UTC
charts do not add rolling queries, history recording or a combined weekly total.
See [chart contract](docs/history-charts-contract.json).
