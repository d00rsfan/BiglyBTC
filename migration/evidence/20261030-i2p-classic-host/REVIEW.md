# Private I2P — ordinary Host and classic UI

Completed the authorised chapter at native `2c6edc1b2390b59ba0f812d3c68e1cbc500e2311` on main. This is the explicitly enabled private I2P preview; the larger I2P/plugin component remains in progress.

The ordinary File/Open dialog imports plain torrent metainfo as I2P-only, preserving the raw info dictionary and original-compatible state/archive/inventory format. The network choice is part of the same durable import journal and survives recovery; absent legacy journal fields retain Public behaviour. Start now, Stopped and Paused are supported. Forged force/queue/verification/allocation requests and incompatible inherited networks/denied Tracker permissions are refused. Loading the file preview preserves a supported start choice.

My Torrents starts, pauses, resumes and stops the genuine private owner. Closing waits for paused/flush/remove/source/provider joins before original progress/counter checkpoint or saved-intent changes. A dedicated Qt socket loop leaves profile I/O and joins outside the GUI/socket loop. Saved pure identity and tunnel settings are reused; missing/changed sources fail explicitly without a replacement key or Public fallback. Statistics/Ctrl-5 has a separate I2P view of actual traffic, peers, verified bytes and tracker observations; My Torrents/Ctrl-1 remains available. No fabricated public/lifetime totals.

## Validation

Fresh final-source private GCC, default-OFF GCC and leak-enabled ASan+UBSan batches passed. Both final-source real isolated Java-router/tracker runs (GCC and instrumented native Host) imported through the actual classic dialog into an empty generated profile, transferred and checked131072 bytes on the original payload inodes, exercised actual UI Start/Pause/Resume/Stop/close and Ctrl-1/Ctrl-5, reacquired the profile lease and rechecked completion after resume. The signed local LeaseSet cache is deliberately warmed; these are local isolated interoperability checks. The SDK/Qt/Java router are not sanitiser-instrumented. Full command/log/source/binary hashes are in the native receipts and per-run public allowlists. Earlier failed attempts remain labelled failures.

Hosted CI initially passed202/210 suites; eight old tests still expected ten Statistics tabs. The same ninth startup test was found locally. Their expected count is now11 with the appended I2P identity checked, and all nine suites are rerun locally. These are test-only changes after the qualified runtime/probe source; source equivalence and subsequent hosted results are recorded separately.

The first integration exposed two native bugs: GetDate incorrectly advertised application0.1.0 as the I2CP protocol version (the pinned router chose a legacy lease-set request), and essential retirement proof was tied to a capped diagnostic alert list. The protocol profile is now2.13.0 and essential events have a separate bounded4096-event scan. Actual dropped/failed/incomplete observations still prevent a checkpoint. Test-only failure exit and statistics signal wiring were also corrected. No new upstream Java bug is claimed here; previous empty-v2/hybrid and streaming FIN reports remain separate.

## Preview and boundaries

`/home/yurii/Java/BiglyBTC/dist-i2p/BiglyBTC --profile ABSOLUTE_ISOLATED_PROFILE --private-i2p OWNED_I2CP_PORT`

The bundle includes Qt and the separately approved private provider. It requires a supplied isolated original-compatible profile, saved pure key/config and an owned loopback I2CP router. It generates no replacement identity and is not enabled in the default Public application. The existing default dist is preserved. Bundle manifest: `e6c5c58663f293ecc27192e64bb1326226edea6d0c90688c26d00280739c5955`.

One active I2P torrent per original identity/profile. Stop before unrelated profile edits. General multi-torrent sharing, full queue/rate policy and plugin parity, public discovery/remote multihop, late tracker response qualification and performance equivalence remain open. DATA/chat/rooms and the next chapter were not started. The running Java application/live profile and original Git HEAD/index were left untouched.

## Source provenance

BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`; official I2P `9134f808337b401e8e53c73734c81fab04280c9d`; libtorrent `740a0b9aeabe00e762cc0efe4a0f27593db2550b`. Modified router JAR SHA256 `b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5`. Reused SDK capsule `9d264115b5964f2ed542d193697b77263f974f0d4c3f929e829bd1c4c1398671`, private DSO `53ce1703a5c43f88d871c6c786c8cc8fbdb7c8c2b058c6520cf733aadded0db9`. No unchanged SDK rebuild or new whole-component parity claim.

Supplemental UI-test source: `e864927ab8548c3f0670d0229d79e6f403133704`. Every application and actual-probe input is byte-identical to the qualified runtime source; see [equivalence](gate-receipts/runtime-source-equivalence.json). Standard hosted CI is tracked in [its receipt](gate-receipts/hosted-ci.json).

Second hosted run at e864927 passed all210 unit suites and explicit stock-SDK refusal; its final smoke still expected ten tabs. The packaging smoke expectation is corrected at `e9546f9893ac9c23086842a49ec4d624c8af5900`; all16 smoke cases then passed locally. Two local fixture-setup mistakes were refused by the existing path guards before the successful run; raw attempts remain labelled failures. The owned temporary fixture was removed after the smoke process exited. No application/probe source changed. Latest hosted CI result is recorded in the final receipt.

Final standard hosted desktop build/tests/smoke and plugin SDK contract succeeded for `e9546f9893ac9c23086842a49ec4d624c8af5900`; see [CI receipt](gate-receipts/hosted-ci.json). No next chapter was started.
