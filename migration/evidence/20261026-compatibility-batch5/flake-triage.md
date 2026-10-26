# Batch 3 sanitizer flake triage — unresolved

Read-only source/log review. No subject, test, compiler, Git or native/shared write. Both failures remain unexplained; the unchanged-source replay is retained evidence, not a causal correction.

`batch3-san-tests-r1.log:419–424` and JUnit `scheduler-host-engine` report single-row `actualRates(native,17,19)` false at `scheduler_host_engine_tests.cpp:60`; multi/inherited rows passed. `r1.log:587–599` and JUnit `network-proxy-owner` report `SOCKS5-redirected-name` rejected “unplanned proxy destination” at `network_proxy_owner_tests.cpp:502`, with actual cleanup joined=1/storageReleased=1. There is no ASan/UBSan error report in this log. Replay `r2.log:21–25,42–54` passes both complete suites; receipts bind the same 898-path source map at source78ed6ff9c18447c210c59edf980f60332d805481.

## Scheduler

The test already observed actual7/9, then confirmed the preference save before awaiting17/19. It does **not** log the latest packet state/values, packet count/sequence or independent tick diagnostic at the failure (`scheduler_host_engine_tests.cpp:34–36,57–60`). Historical Host `intake-next/before/preview/native/app/session_bridge.cpp:2755–2832` deliberately separates confirmed save from `tickScheduler()` application: `saved=true` is not an engine-application acknowledgement. `tickScheduler():2834–2895` can retain a failure after live-settings/config/CAS work; `nativeSession():2431–2448` coalesces delivery. These are relevant branches, not evidence that one failed in r1. A transient lost publication alone does not explain the entire10-second assertion window.

The fixture schedule is all seven days00:00–23:59 (`scheduler_host_fixture.hpp:19`); `plugins/BiglyBTC-plugin-SpeedScheduler/scheduler.cpp:20–23` includes both endpoints. There is no daily gap supporting a midnight/minute-boundary diagnosis. Live application performs actual `apply_settings`→FIFO `get_settings` confirmation and publishes (`persistent_session.cpp:1283–1323`); no source-backed need to relax it was found.

Small useful next change, if revisited: add a failure-message lambda to the **same** assertion, retaining its predicate/deadline, showing bounded latest actual upload/download state/value, session/epoch/sequence/owner state, observation count, scheduler-save result and last `messageReady` diagnostic. This separates rejected tick, missing delivery and wrong persistent limits. Do not replace the latest-packet check with “any old packet matched”, add sleeps or call a new sampler merely to pass.

## SOCKS5 redirected-name

The failure is after the preceding `completed()` predicate succeeded (`network_proxy_owner_tests.cpp:500–502`), so this was not a payload deadline failure. The exact allowlist at275–278 rejected a parsed destination: only the owned tracker, numeric owned peer and two exact remote webseed names/port are accepted. Its `bad():191–193` stores only a generic message; rejected host/port/address type/handshake bytes are not retained. Successful routes are recorded only later, after upstream connection, so their ledger cannot reconstruct this rejected attempt. The r1 output therefore cannot distinguish a provider unexpected route from malformed fixture parsing or stale traffic. Treat the rejection as significant; do not expand the allowlist or ignore an extra attempt after completion.

Source inspection found no demonstrated resurrection of the former double-reader bug: `receive():206–242` has one client reader, forwards only in Relay, and accumulates without reparsing while Opening. SOCKS5 frames consume their validated length before changing phase. This rules out claiming that old diagnosed bug merely from the same generic failure, but does not prove the current parser fault-free.

Small useful next change: preserve the first failed parsed attempt in a bounded fixture-only record (host,port,domain,phase and a short frame hex prefix), and include it plus existing route facts in the **same** `proxy.failure` assertion message. Current32-connection/8192-byte bounds and destination/remote-DNS/source/payload predicates stay exact. No production/provider change is justified until the actual rejected destination is known.

## Exact evidence pins

- `batch3-san-tests-r1.log`: `697cd91626f0c16e5a972ecd74d3ecda82aaa1b30cbb898bca7508667e874422`
- `batch3-san-tests-r1.xml`: `e35892447321c9a80159391952e58bb498a14f44c7c47e38ccc551b5707dcc72`
- `batch3-san-tests-r2.log`: `f059bedbb1585662cd97ffe9bae522f9b0ce3fc304cca95351230a34dae2f6bd`
- `batch3-san-tests-r2.xml`: `0f8d0a0f90199313f07052bd8966a6de246b8307a17f1eb3c0519978c101d452`
- `batch3-san-tests-r1-source.json`: `2f1b18201792161014f06486964e00f2399f0be00bfbed878c246121bda327ad`

Reviewed source pins (unchanged in current native; Host is historical pinned copy):

- `preview/native/tests/scheduler_host_engine_tests.cpp`: `b9e9a7cefa510cf84d1800683ea5d9c3c86f969bd4194e68a461767e8e2b8340`
- `preview/native/tests/scheduler_host_fixture.hpp`: `34e5dd489cd5825fd4bc8ba28785516ab51e67649d5ab9e97a567fb60fccf7af`
- `preview/native/tests/network_proxy_owner_tests.cpp`: `7fcdd33014f0da4595d78f424656102f42871dd9ffa230d6eadbce0d13e337fa`
- `preview/native/adapters/scheduler_service.cpp`: `dcd2f63a59933ed29fa7186206c8f5b5ee2ab064a3b913af01f59b771e5dddb7`
- `preview/native/engine/persistent_session.cpp`: `386348f7fcca5bc0cca9ae461aeabec3e02dd9bb37e39f791bcaa1cdec6b5a56`
- `preview/native/plugins/BiglyBTC-plugin-SpeedScheduler/scheduler.cpp`: `7b6a707e66d77674665e5d213ac3acc5d81e763af3f0bdedf37dc90bf3e6869d`
- `preview/native/app/session_bridge.cpp`: `d47d859dfff1c29969e08666beafdbf45dec55c6f2d49413a94fa3bb24ffbafe`
