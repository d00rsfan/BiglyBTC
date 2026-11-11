# Saved Resume preservation, startup cache and Public chat notifications

Implementation 2171024aa96b0824cac1270b6ba094c0d4edeae4; Native tree 436e4348b7da099477daa53df5d308de5bef60e7.
Original core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f; original azmsgsync 4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. SDK upstream 740a0b9aeabe00e762cc0efe4a0f27593db2550b, overlay parent 4ff478031095830fcfb7531dd6bc0f21b30906af; the previously qualified bounded HTTP400 derivative and signer are unchanged. Exact image and per-component provenance are in RESULTS.json.

A live trace identified an actual Native fault: an otherwise valid saved DONE/NOT_DONE map containing skipped Compact files fell through to whole-torrent hashing. The old admission path called beginUpdate before it knew whether the clean map could be imported, replacing it with RECHECK. No new Java defect is alleged.

## Resulting behaviour

- Admission borrows cached state read-only and qualifies it before beginUpdate. An unsupported clean saved map, an unproven payload snapshot, or missing/corrupt pure-V2 Merkle piece layers is refused before changing Resume or starting implicit hashing. Existing saved RECHECK/STARTED work and explicit Recheck retain their verification paths. A deferred torrent is visibly unavailable and needs an explicit decision; it does not silently damage Java's saved classification.
- Skipped Compact files may retain their validated partial first/last-piece fragments. The metadata-only extent proof rejects a claimed DONE range crossing discarded middle data or a truncated edge. Selected Compact conversion, reordered layouts and pending conversions do not receive this new resume grant. Payload/control identity proofs guard the SDK handoff and unclassified retirement.
- Geometry-proved pure-V2 saved maps can be restored, including the existing sole virtual trailing-pad projection. Every required piece layer is hashed against the info-bound file root before ownership; layer bytes lie outside the info hash. Aligned, short-tail, multiblock-piece and multifile-padding seeds actually uploaded to independently verifying receivers on cold and warm opens. Selected-V2 hybrids retain their separate provider/translation boundary.
- Startup materialises the authoritative global state cache in one pass and publishes its remaining list once, replacing per-admission whole-cache parse/recompression. Only valid primaries are hydrated; missing, recovered, invalid and unknown entries remain. Last duplicate wins; opaque roots/properties, info, Resume and the original primary mtime are retained. Preserving mtime prevents hydration from hiding a payload changed after its saved verification. Stop/crash before commit leaves the original cache authoritative; changed cache or pending acquisition stops publication. Progress is reported without holding the source gate during callbacks.
- Public chat availability recognises actual successful room exchange even before the DHT router's seeded flag. Disk history cannot grant connectivity. The status-bar Chats button counts remote unread messages and opens an unread room. Selecting a hidden room does not mark it read; foreground visibility and selected-room acknowledgement control reading. Local messages do not increase unread.

## Acceptance

Final Resume suite: 17 Qt checks passed in 7.130s, including Compact/DND and pure-V2 cold/warm positives, unchanged-map refusal controls, and explicit Compact checksum verification. The pure-V2 positives include six actual independently verified transfers. The normal V1 Mainline/HTTP/UDP payload/accounting cold-warm case passed (3 Qt checks, 29.978s).

The generated cache suite exercised 96 primaries with one cache publication, duplicate authority, opaque values, compressed/uncompressed primaries, unchanged Resume/mtime, replay, cancellation, abrupt exit, cache conflict and a pending acquisition marker. Two existing writer guard suites and cache/host checks passed (6 CTests); after the admission change the three affected host tests passed again. Generated host fixtures now save their verified maps after writing payload, as Java does.

Chat UI: 8 Qt checks passed. Owned independent chat exchange/history/unread: 3 Qt checks passed. Receive-only fresh-profile Public probes received genuine network messages independently over IPv4 and IPv6; the user also confirmed an actual outgoing message and visible reply in the previously delivered real-profile preview. The agent sent no public chat posts. Provider connectivity and chat-source/UI checks precede the final unrelated storage admission edits; no fresh live-network or user test of the final frozen app is implied.

GCC16, Qt6.12, C++23; no fresh sanitizer claim. Only affected builds/tests and one ordinary transfer were repeated. Build output stayed in ignored local logs. Historical GUI launchers were redirected after explicit approval and obsolete application/SDK copies removed; retain the current build, source and concise evidence rather than rollback applications.

## Remaining acceptance

The user is repairing previously invalidated maps in Java. Already persisted RECHECK entries still require real verification; no completion is inferred from counts, presentation or old backups. The new preview has not been run by the tools on the real profile. Its startup speed, residual CPU and absence of fresh unexpected payload hashing require a retry after Java has completed and exited. This result does not claim whole-library performance parity, all storage/version combinations, whole plugin parity, per-torrent unread support or an explanation of the separate live tracker HTTP400 report.

Reported titles, paths, torrent identities, tracker tokens, endpoints and private traces remain outside public evidence. Neither the live Java process nor real payload/profile was changed by these tools.
