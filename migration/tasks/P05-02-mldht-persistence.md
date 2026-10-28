# P05-02 mlDHT original identity and routing cache compatibility

Continuation after P02-05; user requested remaining plugins without stopping.
Status: identity import/writer codec implemented and qualified as a bounded
read-only slice. The host routing-cache decoder also passes scoped qualification;
operational DHT service, host settings/discovery and real cache updates remain in progress.

Original wrapper runtime ID `mldht`, baseline
`3b12515c5541ed75cfb4f795b672d4e786df9f45`. Audited fork engine
`f9290cbd63805229aef418639b781696c5fa1573`; wrapper POM points to the separate
original-engine revision `067f434eb60323123a5e885093ab2504ab10d5f9`, retained in
`/tmp/biglybtc-deps/mldht-history`. Do not assume the shallow sibling checkout
contains this older upstream object or that either is the shipped artifact.

Characterize exact baseID.config read/write selection and bencoded routing table
fields/addresses; retain original bytes, unknown sections and Java restart ability.
Use generated files and pure original helper/method harnesses only, fenced by
ReferenceFence. Never initialize a live Java DHT/plugin or access the user's
profile. Do not enable network sockets from a cache import or default constructor.

Record full original/source/native hashes per component. Preserve separate MPL2
engine-file notices versus the GPL2-or-later wrapper policies already ported.
Broader actual engine pairing, node-ID derivation, routing/lookup/announce/session,
original bind/proxy/network policies and UI integration remain required.

The [Mainline discovery chapter](P05-MAINLINE-DISCOVERY.md) now qualifies independent host settings/port and owned metadata peer acquisition. Original identity/cache activation and writeback remain open; the bounded read-only codecs keep their earlier status.
