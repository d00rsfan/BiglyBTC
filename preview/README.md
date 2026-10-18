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
defaults. These choices are currently kept for the window session; original
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
cancellation between reader calls. The desktop does not import these layouts
yet; its chooser remains session-only. See the
[layout contract](docs/classic-column-layout-contract.json),
[profile reader contract](docs/classic-column-layout-reader-contract.json) and
[pinned source audit](docs/classic-column-persistence-source.json).

No performance improvement or full migration completion is claimed by this source
milestone.
