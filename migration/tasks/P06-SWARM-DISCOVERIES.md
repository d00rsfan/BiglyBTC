# P06 — Swarm Discoveries / related content

User-authorised 2026-11-11 after active Azureus DHT runtime. In progress; no
completion or next-chapter authorisation is implied. Keep the running Java
client/real profile/payload untouched; qualify generated isolated profiles.

Deliver original related-hash discoveries and their visible classic workflow:
original DHT association keys/values, fresh Public/nonprivate/RCM permission,
source/tracker privacy, bounded asynchronous lookup/publication, saved cache,
unread/deletion/rank/depth/results and source filters, Qt table/context actions
and original configuration controls. Reuse the same shared UDP attachment.
No torrent payload merge, browser/player or new unrelated chapter.

The core maximum is `rcm.max_results` (default500), with
`rcm.max_search_level` (default3). The aercm UI mirrors them via plugin parameters
`rcm.config.max_results` and `rcm.config.max_level`. Preserve existing settings,
including `rcm.overall.enabled`, `rcm.global.filter.active_only`,
`rcm.use.bigly.dht`, names, UI visibility and aercm sources.

`rcm.config` contains either the legacy direct map or an outer `d` blob:
SHA1(core.crypto.obs.id) RC4 with1024 initial keystream bytes discarded, then
GZIP-compressed bencoding. Keep that original key and format; read/write using
bounded decoding, unknown-field preservation and normal resilient config CAS.
`rcmx.config` persistent deletion entries are original eight-byte key prefixes.
No payload/resume/queue mutation is needed by discovery.

Acceptance: focused codec/cache/config/policy tests; generated original Java
wire/cache round trips including Cyrillic/emoji; actual owned shared-socket
association exchange and cancellation/privacy; UI interaction; regression of
shared torrent DHT/chats, affected app build, public privacy review, immutable
source/native provenance, main commits/push and update current preview. Keep
logs local and inspect only summaries/errors. Reuse existing Qt/GCC/SDK cache;
clean generated test roots, keep no old application builds.

Whole distributed keyword-search/remote JSON/subscription integrations and I2P
RCM stay visible as separate later coverage; do not claim whole aercm/F12 parity
from the association workflow. Swarm merging remains near final browser/player.
