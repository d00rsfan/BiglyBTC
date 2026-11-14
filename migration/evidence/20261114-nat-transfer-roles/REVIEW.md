# Explicit DATA external/physical contact roles

Native `21222d769ea99bab77452139ce3701338e648321`, tree `004b67ef1ec7d8bbe80133730ab56b451deb964a`; original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

An explicitly opted-in dual Call/empty-key Write handler can now retain the
original same-IP external/physical contact distinction. ExactEndpoint stays the
default. Read and non-opted-in Call registrations cannot select this role. The
physical peer still must already be admitted; this is a trusted source-owner API
and grants no unknown-peer, routing or STORE authority. Incoming receipts expose
advertised external contact separately from actual socket source; fragments must
retain their initial external address/version. ACK/correlation and network/key/
connection/source/deadline checks stay tied to the actual physical endpoint.
No operational chat registration has changed its default.

The related DATA suite passed in4.32s, including original normal Call/Read/raw,
source/cancel/correlation/replay checks and new foreign claimed-IP/changed declared
fragment identity rejection. Two new SDK IPv4/IPv6 cases passed in0.83s, covering
fragmented call+reply, raw write+ACK, external/actual receipt fields, default strict
handler refusal, invalid opt-in combinations and no send after permit withdrawal.
The ten unchanged SDK raw/encrypted public/private/read exchanges passed in the
initial grouped run. A test-only dispatcher originally compared physical local
listener to generated advertised identity; it now compares actualEndpoint. That
comparison is equivalent for the retained default cases, where both addresses
are identical. Only the two changed-port cases required the focused repeat.

The test changes only the advertised field of a copied owned SharedChannel.
The real SDK socket, physical send, receive source and original channel-current
witness remain in use; Provider::channel and its refusal of invented external
claims are unchanged. This is a generated header seam, not an actual observed
SDK external mapping or real NAT/router test. [Qualification](qualification.json).

This source step supports the separate contact model already compared with the
original constructor/roles/ID. Next comes a bounded NAT actor for generated
client/relay/target, scoped first-unknown/early-inbound admission, room/private
route authority, lookup/publication and actual chat integration. Original
tryTunnel is asynchronous: DH/sync proceed concurrently; preserve its per-node
dedup,45/120s caches and three active tunnel limit rather than adding a sequential
preflight delay. A route observation never changes the full signed private peer
identity or permits public/private payload fallback. Whole NAT/private chapter
remains open. The frozen user image, SDK, launcher and profile remain unchanged.
