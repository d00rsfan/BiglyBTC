# Persistent peer accounting prerequisite

P04-29 supplies the native owner needed before application-session Statistics
widgets and compatible writers can be added. It does not complete Statistics.
The source baseline is original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; the dependency source is libtorrent
2.0.12 at `740a0b9aeabe00e762cc0efe4a0f27593db2550b`.

## Ownership and observations

The lazy libtorrent owner stays on one executor worker. At most one torrent is
admitted, including across single-file and multifile continuations. Opening
Statistics or an offline profile does not create an engine. Application run,
profile session and engine epoch distinguish immutable observation packets.
Absolute observations survive operation boundaries; each existing operation
report keeps its own relative baseline. Optional throwing or missing sinks cannot
prevent essential sample retention. The application caches copied observations;
no libtorrent session or handle crosses into the GUI.

A coalesced timer can request one idle worker poll at a time. Actual monotonic
owner times accompany samples. Missing or failed observations remain unknown;
a failed idle poll publishes an uncertified diagnostic and retains the previous
actual sample. These are peer combined/payload receive/send counters only. They
exclude IP overhead, tracker and DHT traffic and do not claim exact kernel/wire,
crash recovery, or original Java smoothing semantics.

## Retirement and close

Retaining an engine must preserve the old destruction-based storage boundary.
Completion publication requires matching removal, actual weak-handle expiration,
old response drainage and a fresh statistics response from the pinned dependency.
Counter certification is conditional on these source-specific lifetime facts.
Storage uncertainty prevents completion publication and poisons/joins the owner.
A statistics-only failure can preserve safe transfer success while withholding
counter certification. One alert consumer drains bounded batches; there is at
most one outstanding statistics request.

Explicit normal close stops admission, obtains a fresh observation when possible,
and joins the engine before releasing the profile lease. The close handshake does
not permit cancellation after acceptance. Destructor cleanup performs abort/join
without callbacks or inventing a final sample. Cleanup has a bounded certification
attempt; an underlying thread join is not promised to finish within that bound.

## Dependency admission and testing

`BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST` is empty by default. The application then
uses the original per-operation engine path. A nonempty manifest must match an
exact reviewed catalog and complete source, build, installed-header and image
maps. Configure also checks the imported target. Runtime admission checks a stable
nonblocking/no-follow image descriptor, its mapped device/inode and loaded ELF
identity, and six resolved libtorrent exports. A caller flag, matching version,
preload, second image or edited source tree cannot confer admission.

The source-built unmodified dependency and separately cataloged instrumented test
providers have distinct manifests. Instrumented providers expose private gates
around real positive receive completions and successful write-job retirement,
plus a bounded event ledger. They exercise delayed completion, removal, weak
expiration and fresh-response ordering. Synthetic error/drop selectors test policy
through genuine alert posting and the same real popped batch; they do not claim
natural allocator pressure or a forced kernel error-plus-positive-byte result.
Test fixtures use generated profiles, random numeric loopback ports and disabled
discovery. Instrumented providers are qualification artifacts, never portable
runtime dependencies. Manifest paths identify retained local qualification trees;
they are not general installation or trust overrides.

No original configuration, torrent temporary data or live Java process is used
by qualification. Existing state/payload publication bodies and convenience APIs
are preserved. No lifetime, mark, minute-history, cache or configuration writer
is introduced. Persisted accounting and the new visible session widgets remain
separate planned work.
