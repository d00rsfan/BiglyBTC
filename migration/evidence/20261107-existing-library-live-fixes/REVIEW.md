# Existing-library display and startup fixes

Original: cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
Implementation: c066523d0dfa75c3bb58b253ebc842df5a376b48.
Native subtree: 8515c321a7103e5013a53ddc81601dbe1be8b28c.
Earlier startup fix: 9e0b3ce9150523034e86d8c35bfb38d40c5a73a2.
Root reviewed source and evidence; no independent reviewer.

Original # is the displayed Order column: the saved position within its downloading or seeding group. Import now also covers savepath (Save Path) and trackername (Tracker Name), including each pane's original visibility, width, order and sort. Supported single-file destinations follow original file links. Tracker display uses hosts, preferred hosts and original domain abbreviation, omitting URL credentials, path and query. Other compatible existing native value columns are imported. Unsupported enabled IDs are listed in RESULTS.json. Original down is deliberately not mapped to raw Received because Java subtracts discarded and hash-failed bytes.

Presentation now uses completion excluding DND and keeps a known seed in its pane during checking. Checking XX.X% comes from the actual engine sample, independently of Done. UI completion grants no payload or network authority. Missing global cache after clean Java Exit falls back to primary metadata; stable presentation facts use inode/size/nanosecond-mtime/ctime memoisation. Background permission scans consume one fresh global cache per pass. Start and per-I/O permission checks remain authoritative.

Both GCC and ASan/UBSan passed 10 classic CTest suites and 18 selected Qt data rows per lane: 5000-row libraries with/without cache (2), DND completion (2), tracker redaction/file-link destinations/refresh invalidation (4), one cache read for 64-row permission scan (1), actual Mainline+tracker payload transfer and warm reopening over IPv4/IPv6 with UTF-8 and ampersand filenames (5), controls (1) and read-only integrity checks (3). Long accounting-idle tests were not repeated. Previous privacy packet-capture evidence remains separately scoped, not relabelled as fresh. Failed compiler/fixture attempts remain local; only live-r6 is accepted. RESULTS.json records commands, image/receipt hashes and coverage.

The real-profile preview read 4616 rows in 1290ms and was ready after 3247ms while the sanitizer build was active. The user confirmed the display looks good. No automatic backup was made; the agent-owned earlier backup was deleted on request, and the user-owned copy was only read. User torrent identities, tracker URLs and payload paths are not published here.

This bounded milestone does not finish the daily-use chapter. Unexpected hashing after clean Java Exit, configured checking concurrency, startup Waiting states, and discovery/scrape fairness above 256 active hashes remain open. JAVA-CHECK-SCHEDULING.json records the Original policy and the user's saved options. No global/component integration state or next-chapter work is advanced.
