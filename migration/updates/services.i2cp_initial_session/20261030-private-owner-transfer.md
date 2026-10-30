# Bounded private-owner integration review — 2026-10-30

Components: `services.i2cp_initial_session`, `engine.selected_swarm_admission`. Reviewer: root/Codex; the existing three authorised agents independently authored/reviewed source recipes, while root performed adoption, execution and measured artifact review. No additional independent human review is claimed.

This is a bounded initial integration, not an upstream update. Old integrated A remains null for these source coverage entries; candidate B remains the recorded baseline: BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, i2p-java `9134f808337b401e8e53c73734c81fab04280c9d`, azneti2phelper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`. URLs remain https://github.com/BiglySoftware/BiglyBT.git, https://github.com/i2p/i2p.i2p.git and https://github.com/BiglySoftware/BiglyBT-plugin-azneti2phelper.git. Native library source is https://github.com/arvidn/libtorrent at `740a0b9aeabe00e762cc0efe4a0f27593db2550b`. No baseline or integrated-through coverage is advanced.

| Relevant effect | Disposition |
|---|---|
| Original saved profile/key/full torrent identity and payload ownership | Implemented for explicit owned I2P admissions with original locking/storage; no new persistent conversion |
| Authenticated local owner/purpose/port bridge and dedicated ordinary provider | Implemented, opt-in only; public/mixed/direct fallback refused |
| Original tracker source port 0 and exact return ports | Implemented and signed-handshake regression qualified |
| Private withdrawal + ordinary destructor shutdown ordering | Corrected with distinct fixed storage and deterministic lifecycle plus real transfer/join evidence |
| Default Host/UI, public discovery, multihop, complete plugin equivalence, performance | Deferred to remaining plan; this bounded evidence cannot advance full component coverage |

Implementation begins at `078cc91db1aff4195986beb860063aad3f8b6967`; outgoing port correction is `797638b44b8f9a621e9a6d718a94472d0b242297`; bounded diagnostics are `8b17980cc769b553375e01de53bf9ef8a5930672`; final qualified source is `9f46cf05d133624e48c6d60d45d2c503a6141ff9`, all in https://github.com/d00rsfan/BiglyBTC.git. Provider source freeze `9d264115b5964f2ed542d193697b77263f974f0d4c3f929e829bd1c4c1398671` and effective cumulative patch are committed in the evidence. Current results are nine fresh SDK CTests, native11/11/11 plus default4, both hosted checks and an actual 131,072-byte two-owner tracker-discovered transfer with joined disk/inode verification. Exact command records and failures: [review](../../evidence/20261030-private-i2p-provider-transfer/REVIEW.md).

Result: bounded section qualified; both component states stay `in_progress`, all source coverage nulls/baselines/reviewed fields preserved. Stop before any following chapter.
