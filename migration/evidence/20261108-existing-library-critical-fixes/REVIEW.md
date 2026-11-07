# Existing-library critical fixes

Original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
Final implementation 6181803356a8bf208bd8cb4602ec0273af04f8dc.
Native subtree ee6d4765f61ea70bea8cc7432d04a5ef5dfad047.
Main package 18fcc1d6b77ed8d2c6b0ffde826bc070312a7305 / Native 1c0f23f0edf368e07c0d19aa4a2c6989121b40e5.
Root reviewed source and evidence; no independent reviewer. This package does not complete the daily-use chapter or the global migration plan.

Classic view reads Library.ShowTabsInTorrentView (classic default false). With details hidden, selecting a row does not request hidden Files data. Double-click mode1 opens a closable torrent tab; multiple tabs retain their identity and inner-tab selection. Explicit inline details remain available. Other explicit double-click modes are unavailable; an absent key retains the Native details fallback rather than Original action0. The lower version/DHT/rates status bar is deferred.

Sizes and rates use original unit settings; numeric sort roles remain exact and headings omit fixed bytes. Both tables render ascending as an up triangle and descending as a down triangle. All15 columns in both panes are checked in both directions, including mixed B/KB/MB/GB/TB values and real header clicks. The screenshots below use generated tables only.

![Complete ascending](complete-size-ascending.png)
![Incomplete descending](incomplete-size-descending.png)

Accumulated uptime uses fixed365-day duration years, then days/hours/minutes/seconds; live tooltips retain seconds. Calendar months have no origin in accumulated sessions, and leap days still contribute their seconds. Force Start already reached the engine; status now displays Forced Seeding/Forced Downloading, including first-priority seeds. Generated-profile tests cover cancellation, forcing, joined close and reopening for complete/incomplete torrents.

The two additional compatibility cases are represented only by synthetic data. Authenticated pure-v1 metadata declaring UTF-8 can project its original safe filenames into a private LT metadata copy, retaining U+200E and the original raw info hash and geometry. Original dht markers are filtered from runtime tracker endpoints. Omitted UDP tracker ports use Original's80 only in existing-library runtime copies; raw metadata, userinfo/path/query and private DHT/PEX restrictions remain unchanged. Invalid explicit ports still fail. Policy errors expose only a bounded field label, never the rejected URL or credentials.

The earlier resume/check-policy implementation276d901c4cd353a35ecb21623d6be43e66c2778b is included. Valid primary pure-v1 unmapped linear piece maps avoid hash reads; clean checked partial maps are retained. Invalidated maps, changed payload, mapped layouts and v2/hybrid resume retain conservative validation. Old-preview invalidations can still require a check. Global/per-filesystem limits and smallestfirst on/off are tested. Checks keep permits through cancellation/pause drainage. Device IDs conservatively approximate Java FileStore; friendly pacing, hot policy updates and partial-block import remain separate work.

Startup publishes restoring-transfer progress and favours incomplete rows within its priority groups. Fresh permission scans share one filename context/config read across300 subjects. Mainline active/queued-target bounds are8192; tracker-response bound65536. Dispatch is round-robin, at most one hash per125ms, ordered by hash suffix to spread Kademlia prefixes without changing wire hashes or authorisation. A64-loopback-router normal-policy fixture observes successful token-bearing get_peers/announce_peer for all300 subjects while preserving flood protection. These owned fixtures do not prove Internet reachability.

Final normal GCC coverage is enumerated in RESULTS.json. Full ASan/UBSan qualification belongs specifically to the main-package predecessor above. Final compatibility changes additionally have focused ASan/UBSan coverage: the two changed engine modules plus their harnesses are instrumented and linked against unchanged normal libraries. GUI follow-ups have normal runtime/interaction tests. No claim is made that every final translation unit was freshly sanitised. Passing tests are reused only with byte-identical freshly rebuilt executables; failed attempts remain failed. Long idle-accounting tests were not repeated.

Fresh eight-case IPv4/IPv6 wire capture confirms private hashes remain absent from public DHT and tracker credentials appear only at their intended HTTP/UDP tracker; public-torrent keys remain absent from KRPC. Real profile metadata was only read for diagnosis and is excluded from Git, along with real names, torrent hashes, data paths and URLs. Public receipt paths use workspace placeholders; raw local evidence stays outside the repository.

The local GCC16 Release configuration keeps three existing expected/optional template warnings nonfatal in their specific files only: inbound_responder.cpp and session_bridge.cpp (maybe-uninitialized), subscription_acquisition.cpp (free-nonheap-object). Warnings remain visible; other warnings remain fatal. The pinned external CMake include changes no Native source or runtime logic. Failed unmodified Release attempts remain recorded.

The running old preview was neither stopped nor overwritten. Full sanitizer receipts were stamped before reusing the independent historical san cache for normal GCC16 Release/O2. The normal executable has no libasan/libubsan dependency and the role marker records the cache change. Qt/provider/default-distribution files remain pinned; no new profile backup, Qt copy or rollback build was made. Close the current Native normally before using dist/BiglyBTC-daily-use with the explicit profile. This round stops after publication; no next chapter is started.
