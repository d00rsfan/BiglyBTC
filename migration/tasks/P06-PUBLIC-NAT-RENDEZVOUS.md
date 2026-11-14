# Public NAT rendezvous/tunnel dependency audit and implementation

Remain in the private Public-conversation dependency chain. Pinned BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; azmsgsync `4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`.
Original MsgSyncHandler.getSharedSecret invokes tryTunnel before DH contact.call;
ordinary sync also has tunnel-related paths. Current qualified native private
routes cover direct owned IPv4/IPv6 endpoints, not NAT-assisted peers.

First audit DHTNATPuncher/Impl, DHTPluginContact.openTunnel, MsgSyncHandler
tryTunnel and exact original handler keys/transfer modes/packet and value
formats. Locate reusable native transport/database/router authority. Do not
invent a wire format or infer required behaviour from method names. Separate
codec/owned state qualification from actual owner integration; record all full
original/native/plugin hashes per bounded component.

Use generated owned three-party endpoints/mappings and actual original-method
interop where practical. Existing shared UDP listener/port/source policy remains
authoritative; every effect checks actual endpoint, receipt, deadline, network
and owner generation. No eager real peer/router/profile probing, new listening
port, unencrypted/private-public fallback, tracker secret publication or external
payload/peer contact. Do not make private payloads rendezvous/DHT values.

Proceed autonomously through independent work; no new agents/daily image/runtime
manifest/SDK/launcher changes or full backups. Only request user involvement if
an actual required external authentication/acceptance step cannot be qualified
with owned fixtures; otherwise continue unblocked plan work. Batch meaningful
checks and inspect concise summaries/errors. Then return to remaining private
connecting/popout/sidebar/refcount/keep-alive/read-focus and original window
preferences; private windows must not restore as public rooms after restart.

2026-11-14: bounded empty-key DATA transport qualified and pushed in `936a0f2bab2d67f3c4c13662e0acce27c2e7c8df`. [Review](../evidence/20261114-nat-empty-key-write/REVIEW.md). Continue exact contact/message codecs and three-party NAT owner; whole NAT/private chapter remains open.

2026-11-14: [NAT wire format](../evidence/20261114-nat-wire/REVIEW.md) qualified in `9cf59d6124548cdb850a6efdd9d7568b56917235` with100 exact original builder examples. Full original UDP static initialisers excluded from memory-only oracle. Next finite binding/client/punch policy and source-bound observed-port ingress, then actual owner/publication/chat integration. No chapter completion or real NAT claim.

2026-11-14: [Server binding policy](../evidence/20261114-nat-bindings/REVIEW.md) qualified in `3f7b61f6ca2175fb525407ff4a7664703ec79329`,75 exact original commands plus retained100 wire builders. Next client bind/punch lifecycle, expected observed-port authority and actual shared UDP/publication/chat owner integration. Secondary rendezvous de-duplication remains outside current primary-server policy.

2026-11-14: [Client/tunnel policies](../evidence/20261114-nat-client-policy/REVIEW.md) qualified in `0a510589e5eec530d3694b9745e964be292a749f`,30 exact original bind-result commands and native clock/source/port tests. Next separate external/transport roles (bind reply reports external port), source-scoped DATA/first NAT/tunnel admission and actual owner/publication/chat integration. Keep signed private identity authoritative; frozen preview/profile unchanged.

2026-11-14: [Separate contact roles](../evidence/20261114-nat-contact-roles/REVIEW.md) qualified in `d94740235671a6dc596ecbbb2eef2201b5734d55`,56 exact original constructor/role/ID cases and native initial physical-route retention tests. Next source-scoped NAT/DATA admission, early inbound observation and actual bounded three-party shared-UDP owner; lookup/publication/chat integration follows. No actual NAT/whole chapter completion claimed.

2026-11-14: [Explicit DATA contact roles](../evidence/20261114-nat-transfer-roles/REVIEW.md) qualified in `21222d769ea99bab77452139ce3701338e648321`, actual SDK IPv4/IPv6 physical delivery with generated declared-port seam. Already-admitted peers only; defaults strict, no application activation. Next bounded three-party actor, typed unknown/early-inbound admission and source-scoped room/private route updates. MsgSync tryTunnel is asynchronous (max3active,45/120scaches), so preserve concurrent DH/sync rather than sequential NAT preflight. Lookup/publication/app integration and real NAT acceptance remain open.
