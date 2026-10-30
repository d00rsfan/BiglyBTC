# P07 — private I2P in the ordinary Host and classic UI

State: implementation in progress. User authorised this chapter on 2026-10-30; finish it and stop before the next chapter.

Source baselines remain BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, helper33d97eca67a9f332e3d4da7e7492ec54f4bec078 and official I2P9134f808337b401e8e53c73734c81fab04280c9d. The separately qualified modified router artifact remains b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5. Native provider parent is9f46cf05d133624e48c6d60d45d2c503a6141ff9, SDK capsule9d264115b5964f2ed542d193697b77263f974f0d4c3f929e829bd1c4c1398671. New implementation hashes follow actual commits and checks, never future placeholders in the registry.

## Scope and acceptance

- Explicit private-I2P application option, using an owned numeric loopback I2CP endpoint. Default Public engine behaviour and its separately approved provider remain unchanged.
- Original full metadata, I2P-only saved network, plugin enable intent and saved pure identity key. No transient replacement key, generated identity, Public fallback or system SAM service.
- Ordinary Host worker commands and classic My Torrents actions: start, pause, resume, stop and explicit window close. One dedicated Qt socket event loop; filesystem work and actual provider/source joins stay external to that loop and GUI.
- Graceful stop must use actual paused/flush/remove/join fences before original progress/counter checkpoint and saved-intent mutation. Missing proof preserves recheck and reports failure.
- Separate observed I2P traffic/peers/tracker/error presentation in Statistics; never manufacture an exact public epoch, lifetime sum or close tail.
- Focused Host/UI regressions and a fresh isolated real router/tracker, actual ordinary Host admission and131072-byte payload verification after joins. No stale earlier acceptance credited as a new run.
- Build an explicitly labelled private preview with dependencies and publish source/evidence on main. Original running Java client/live profile untouched.

## Present boundaries

One active I2P torrent per original pure identity/profile. The explicit private application mode is exclusive with Public managed/magnet owners, avoiding unqualified cross-owner payload/path/inode overlap. Ordinary unrelated profile writes must wait until stop; unexpected source changes withdraw authority. General multi-torrent sharing, original queue scheduling/rate policy, public discovery/remote multihop, late tracker-response qualification, complete plugin compatibility and performance equivalence remain open. Chat/DATA/rooms remain later work.
