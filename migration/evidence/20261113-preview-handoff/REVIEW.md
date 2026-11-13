# Qualified preview ready for the next normal Exit

The replacement executable includes the qualified Swarm Files/properties work,
immutable configuration/gzip snapshots, strict batched mapping validation,
bounded prompt startup continuation, engine-owner API pins and authenticated
selected-v2 hybrid warm resume. Implementation/source tree:
55e18aa6addb8bd829273727471c4339144280b7 /
09430e0c5b9cd7c7df2f1d3a7dc9870ec65b3f08. The SDK remains unchanged.
[Performance evidence](../20261113-library-performance/REVIEW.md) and
[warm-resume/original Java evidence](../20261113-selected-v2-warm-resume/REVIEW.md)
record exact qualified scope and remaining real-profile acceptance limits.

Frozen image SHA256 `9ccfe0a63ca999fd28090ad874043734e20c655659a0319600f20461207220fa`,
19805776 bytes. Runtime/provider input validation passed. The running preceding
image and profile were left untouched. The same generated chat-preview entry
now routes the next launch to this replacement; daily-use forwards that entry.
No application was launched during preparation. After normal Exit, the next
user launch may capture one compact private service-state handoff under the
profile OFD lock, compare it with the previous private baseline and retain the
new observation baseline. It opens no payload and writes no profile or full
backup. An owned contention test proved capture refuses a held profile before
metadata access. The prior image is retired only once its runtime flock proves
it is no longer running; no rollback build is archived.

New distributed keyword search was explicitly deferred. Its incomplete query
prototype is versioned under migration/drafts/20261113-related-search and is
excluded from normal application sources/build; the binary has no SearchQuery
symbols. Actual original matching characterisation passed, but its prototype
Unicode differential has an unresolved case. This is not a released feature.

The user's final boundary is STOP after preparing this build. No next chapter
is active. Future work requires the next user instruction; actual repaired
library/network/visual acceptance remains pending.
