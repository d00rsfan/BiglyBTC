# Original Mainline ownership and standalone tracker scrapes

Product source:
`210148e403a9e3b0b539d0c195a44e5bb661ba6c` on native `main`, over
`0f32a6bfbb0e2d5e8f1d842c932df4c45f4f08ef`. The complete before/after map
of 59 files is `build/mainline-host-cohort-20261028/source-adoption-r4.json`.

This batch adds retained-profile original DHT identities, missing-root creation,
dual-family identity/routing separation and a default-interface bind planner.
Saved-library scrape scheduling can use a separate UDP or HTTP(S) transport
without admitting the torrent or opening its payload. HTTP redirects require
fresh owner approval before each DNS request and socket; gzip decoding has
explicit compressed/decoded limits. The development bundle includes Qt's
OpenSSL TLS backend. The Tor child watches retained directory ancestors.

The first build, source `55fda62192e4bece4076c06da561084125b7ef38`,
failed before runtime tests on all three
lanes. Missing direct UDP-header includes affected GCC/Clang/SAN; GCC also
reported a redundant optional-reset destruction warning. Both are corrected
without warning suppression. These failed runs retain zero runtime credit.
The later source includes these corrections, an independently reviewed
HTTP deadline correction and the fix for two unbuilt hosted Tor unit fixtures.

The second attempt compiled all Clang targets, then its pre-test inventory check
found an incorrect expected timeout in the qualification recipe (network-policy:
actual 240 seconds). GCC found one unnecessary copy in a test range loop.
Neither attempt receives runtime credit. Both corrections preserve all test
assertions; the revised recipe keeps the same selected suites.

Fresh GCC, Clang and leak-enabled ASan+UBSan passed 19, 16 and 19 selected
suites, respectively: 103, 80 and 103 Qt method rows, all passing. The three
long UDP/single-tracker/HTTP suites are compile-only in Clang and executed in
GCC and the sanitizer lane. No historical passes are pooled into these totals.
All three processes joined with exit zero; source, provider and binary checks
passed. Full-client and release qualification are not implied.

The third run built all three lanes and executed the selected suites. It found
a real cold IPv6 direct-request bug: selection used an unset external-address
vote rather than the installed UDP socket. An owned packet trace confirms that
only the IPv4 PING was sent. A two-file provider correction and regression have
passed source review and its fresh artifact passed 28 provider controls. Other failures concern
test prerequisites or expectations: private generated directory permissions,
earlier ancestor-watch rejection and the healthy-tracker fallback gate. Their
corrections preserve production guards. Those earlier invocations remain failed;
the fourth invocation above tests the corrected source and new provider.

The provider is built from upstream
`740a0b9aeabe00e762cc0efe4a0f27593db2550b` plus the recorded native changes.
Its complete source/build record is
`623778fc3a5b25144e842a39154dab4cd107e4f115bc6474dd973f67888582d3`;
the tested DSO is
`355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443`.
The 27 exported controls, 282 installed headers and complete SDK/source maps
were checked independently before native qualification.
The complete provider patch is in `provider/`. Applying it to the pinned public
source archive reproduced all 1,082 source files exactly; the archive digest
and the previously recorded upstream Git attribution are distinguished in the
patch information file.

Source `6315055985dbd2bb6a0ebac760ff49326363fcc0` separately passed the
original UDP decoder comparison (13 cases, 78 scalars) and extracted offline Tor
under GCC and leak-enabled ASan/UBSan. The GCC Tor socket trace contains only
127.0.0.1. These subjects grant no private-distribution or full-cohort pass.

Current-source extracted offline Tor also passed GCC and leak-enabled
ASan/UBSan. It authenticates and stops an owned real Tor process with public
networking disabled; the GCC trace contains only 127.0.0.1 socket addresses.
These checks do not start or modify a system service.

Private-distribution checks passed all three selectors: original dual-family
wire identity, the GUI smoke and actual owned HTTPS root/hostname validation.
They used the relocated provider, private Qt libraries and private TLS backend;
the runtime's OpenSSL 3 libraries are explicit OS dependencies. The checked
bundle contains 119 leaves, including the newly packaged TLS backend, and its
manifest SHA-256 is
`60d71fd3b2f991523f7ae6a0a399663dcfabf7b796ed4050a707e65b160ea83c`.
It replaced the current `dist` atomically; the previous preview was deleted.
Selected executed receipts and test XML are in `local/`; `verification.json`
records their digests and the precise scope.

Earlier private failures remain recorded. The first attempt stopped before
staging on timestamp-only Qt-autogen dirtiness; tested ELF bytes remained
exact. After removing that unnecessary no-work expectation, staging and the
dual-family wire selector passed. The GUI smoke then exposed a missing test
fixture environment variable. Both failures are retained. The corrected runner
prepares its own temporary sample only for that test, without adding sample
rows to a normal user profile. Neither runner correction required recompilation.

The normal application was opened against the fresh `/home/yurii/.bitlybtc`
profile, with an actual empty inventory, separate loopback TCP/UDP port 60481
and no sample torrents. Its running image maps use this dist's provider and Qt.
The startup receipt is `local/normal-empty-profile-open.json`; ordinary window
shutdown has not yet been observed. This is an empty-profile launch check,
not a transfer or comparative performance qualification.

Full Mainline/HTTP/Tor parity, public operation and release qualification remain
open. Multiple local addresses per family, broader original redirect/response
budgets, saved certificate handling, proxies/credentials and other documented
tracker dialects require further work. These changes do not close P05–P13.

Only generated owned fixtures are used. The running Java client, original
profile/payload, ordinary original HEAD and original index remain protected.
Keep one current development distribution; early sample previews are not
retained for rollback. Release rollback work belongs to usable release
candidates, as clarified by the user on 2026-10-29.
