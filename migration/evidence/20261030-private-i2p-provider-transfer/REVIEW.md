# Explicit private I2P provider and original-profile torrent transfer

Qualified native source: `9f46cf05d133624e48c6d60d45d2c503a6141ff9`. This bounded section is complete; `services.i2cp_initial_session` and `engine.selected_swarm_admission` remain in progress. Full migration, default backend, release and performance parity remain open. No following chapter was started.

## Actual results

| Gate | Result |
|---|---|
| Private SDK | 9 fresh named CTests, including deterministic actual-session withdrawal/destruction; 31 required same-image exports |
| Native Clang / GCC / leak-enabled ASan+UBSan | 11 fresh affected CTests each, plus compiled owner archives; GCC also built the actual transfer probe |
| Default OFF GCC | 4 fresh controls and BiglyBTC compiled against the unchanged default provider |
| Actual transfer | Two normal LT owners, separate original-compatible profiles, real tracker discovery and Java router; 131,072 verified bytes at both original payload inodes after both joins |
| Negative controls | Wrong capability refused by both owners; HELLO expires while source access is held; replacing the saved key withdraws the owner without new admission |
| Isolation / cleanup | Fresh five-namespace loopback fence before and after; three real clients use accepted signed Enc4 LeaseSets; all clients joined, no public transports, private fixture keys/profiles removed |
| Hosted CI | SDK contract 41053224420 and desktop unit/smoke 41053224451 both succeeded for the exact qualified source |

The real transfer took 27,201 ms and produced SHA-256 `c094f75a988b470c822f157431f2b920bd750803225be7d89374eced4a070147`. There was no direct peer injection, seed-mode assertion or fabricated piece completion. `acceptance/run/native-result.json` records exact joined disk bytes and inode preservation. The finite application and loader evidence is retained; declared Qt mapping is not a relocated-dist qualification.

The SDK DSO is `53ce1703a5c43f88d871c6c786c8cc8fbdb7c8c2b058c6520cf733aadded0db9` (9476640 bytes), source capsule `9d264115b5964f2ed542d193697b77263f974f0d4c3f929e829bd1c4c1398671`, record `5c3653a6c628fd0f6ac6df1e75bae50dfded310dc84c89c079ec8df1a1ec28c3`. The source/installed/header maps contain 1087/293/284 leaves. The separately measured private approval is `6ff4c4d044777e2d777a7b61271c804736a102be2821e65840529cc0ecf529d5`. Native sanitizer coverage does not imply that the separately built provider DSO was sanitizer-instrumented.

Exact commands, exit codes, tool/dependency/image hashes and XML are in `sdk/runs/r3/commands.json`, `native/<lane>/commands.json` and `acceptance/run/namespace-command.json` / `commands.json`. `recipes/finish-gates-r3.py` records the sequential launcher. `provider/optional-private-i2p-provider.patch` is an additive thirteen-file child against the retained default original-routing provider; its read-only apply check passed. No binary/SDK/test ELF/class copies are published here.

## Corrections and retained failures

1. Outgoing tracker Streaming uses source port zero. The manager incorrectly required both ports to be non-zero, closing authenticated tracker sockets with EOF. Only outgoing local port zero is now allowed; remote port, listener guards and exact reply tuple remain fixed. A real signed 0-to-80 SYN / 80-to-0 SYN-ACK regression also refuses wrong reply ports, zero listener port and zero remote port.
2. Synchronous private withdrawal queued stage two in the same single-slot allocator subsequently used by session destruction's call_abort. The latter could overwrite the pending stage, leaving scheduler/disk work guards alive. A distinct preallocated stage-two slot preserves the no-allocation abort contract. The deterministic real-session external-executor test forces destruction before draining the queued stage and now passes; actual two-owner transfer/withdrawal/join also passes.

Reliable shutdown exposed old test code querying an already drained executor. Test-scoped work guards preserve every old status assertion; the new deterministic row invokes actual aux methods inside its controlled executor. Only that test was rebuilt after the full SDK build: all 167 production objects, installed SDK and DSO stayed byte-identical. The intermediate raw-cache byte guard failed after successful configure; the actual ABI values were equal and Ninja dry-run proved exactly one test compilation and one link. The final continuation checked semantic configuration, reused that successful configure and ran all nine controls fresh.

Earlier missing public LT include / protected-profile payload fixture failures, hidden-object test linkage failure, actual transfer timeouts, pre-fix progress/stacks, the stopped old SDK unit and the cache guard failure remain recorded as failures. They are not counted as current passing tests. Source/amendment maps, reviews, historical receipts and finite diagnostic outputs are retained under `history`, `fixes`, `diagnostics` and `sdk/history`.

## Source provenance and limits

BiglyBT baseline remains `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; official I2P source remains `9134f808337b401e8e53c73734c81fab04280c9d`; plugin source remains `33d97eca67a9f332e3d4da7e7492ec54f4bec078`. The actual modified JAR is SHA-256 `b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5`, distinct from a claim of whole official/modified source equivalence. Native library upstream remains `740a0b9aeabe00e762cc0efe4a0f27593db2550b`. Baseline/first-ported/integrated/reviewed component coverage fields are preserved; no whole upstream/plugin is declared verified.

This qualifies explicit optional private owners on a same-router zero-hop fixture with a warmed signed LeaseSet cache. Default Host/UI activation, public discovery, remote/multihop tunnels, general contact/alias churn, late tracker-response qualification, complete original plugin parity and performance/resource parity remain open. Chat/DATA/rooms were not started. No new upstream Java defect was established in this section; the existing FIN/close-callback report remains separate.

Default DSO `355f0c7bca7404c3199e85d4741a3734ac460c79d641a2539c049c5a8ad0c443`, binding `f746fd279755ea90f31e20b0c9131c46e5f0871c4aab3665f232975f98feb56e`, and single dist manifest `aad928753cb8f2dac8f99245298d987cfe2c4bee9e7bdebf5b8d7645ed6d559b` remain unchanged. All 119 dist leaves were rehashed and no extras were found. The running Java profile/payload was untouched; ordinary original HEAD/index were preserved. Work stops after this section.
