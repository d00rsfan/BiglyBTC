# Mainline peer PORT source coverage, 2026-10-28

Status: in progress; this proposal adds source provenance only. Native execution and shipping claims remain pending root evidence. It advances no original integration or first-port baseline.

Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, `PEPeerTransportProtocol.java` SHA256 `56d269aa533788742b61f26b5b065520f0aa720a4c30339425cf388be62367eb`: send1898–1906, receive3767–3778 and dispatch4850–4852. Wrapper `3b12515c5541ed75cfb4f795b672d4e786df9f45`, `MlDHTPlugin.java` SHA256 `fe1011f995335ed16461f9c1aaf2c56d6abca63b79da53e29e83643f45b2965e`,269–300 supplies the Mainline port and delegates public peer address/PORT hints. No source-update ancestry or whole-plugin equivalence is asserted.

Native implementation `d1b15d4368ec1af19d4ba6f758005fae922562c9` separates the torrent session from its dedicated Mainline service. A per-admission callback carries the actual connected endpoint, wire hash/protocol and bounded sequence; the owner withdraws ineligible/retired/overflowed observations. A correlated real PING plus typed live routing-table observation precedes progress. Saved contacts include replacement nodes and confer no verified-live authority.

Dependency source `740a0b9aeabe00e762cc0efe4a0f27593db2550b` has an eight-path child delta, retained beside this review. Parent record `9f8d1f436f943994a117f9f33bbaf016f92bb60ab3c0cecf9e0b6f02efb3c6ee`; child record `c848411975cecc955370cf0a95790fd4659a52714b8f57ce9c82fa7e9fbf54d3`; DSO `9fbb550bbc1f877ce34ce21849deab97971f5d3eacfbc531a7b777d28a3b2eea`,9153448 bytes. New header SHA256 `5b4c84a034e7ddc88f23adff559926de351294bed50292b298031013f9e43690` defines a72-byte POD event and three additive exports. It grants no capability by itself. The private gate retains the same full20-export/source/SDK/hash/mapping-bound image.

Root must append actual final native lane, guard/deployment, owner and relocation evidence before publication. Initial sandbox socket rejection remains uncredited history. No original cache/ID/profile conversion, internal torrent DHT activation, alternate private route, P06/Azureus DHT substitution, Java/LT identity equivalence, whole-client or performance closure follows. All prior components, provider cohorts and baselines remain unchanged.

## Actual bounded qualification

Native `18c3e0048d381748c6a4154b3d6e9100b6ec2dda` passed gcc:23, clang:5, san:5; actual private relocation passed. See [verification](../../evidence/20261028-mainline-port/verification.json). The earlier source-only proposal remains history; broad baselines remain null and state stays in_progress.
