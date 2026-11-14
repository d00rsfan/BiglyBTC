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
