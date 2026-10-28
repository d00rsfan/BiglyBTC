# P05 network runtime reliability

Status: verified bounded chapter; full CF03/CF08/P05 remains open. User authorised the next chapter on 2026-10-28.

Native starting commit: `e48d70de8bae723d3062c58d40e1fae6a036d4bf`.
Original Java reference: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Provider upstream: `740a0b9aeabe00e762cc0efe4a0f27593db2550b`.
Exact parent provider: SHA256 `98279a468e0fa5147876b6f22f8947f432c294dfc02fb50cf179fc9472b3b51f`;
source/build record SHA256 `91218c17d3e74bb79a0fea0cfbbf3734e3422895e59f46fad4cb6b17e2927fd0`.

Resolve the observed zero SOCKS CONNECT through a deterministic cancelled resolver
completion. Guard every handshake continuation and retain each attempt's buffers;
old completions must not affect a fresh connection. Keep authentication, destination,
DNS and no-direct-fallback rules. Preserve valid UDP ASSOCIATE framing.

Allowed changes: isolated new provider source/artifact/record; native runtime binding
and private stager selection; focused provider/native regressions; generated-file
read diagnostics; task/evidence/provenance records. Shared source lock belongs to
root. Workers use disjoint staging folders under `build/network-reliability-20261028`.

Acceptance: actual old-provider causal reproduction with ordinary/reuse controls;
fixed cancellation/reuse/error tests; exact source/installed SDK/artifact checks;
affected owner tests on GCC, Clang and leak-enabled ASan/UBSan; configure/runtime
negative controls; relocated private development runtime and owned payload checks.
Store complete build output in logs; read status and only relevant failures. Reuse
unchanged evidence explicitly rather than repeat unrelated suites.

Non-goals: whole CF03/CF08/P05 closure, public network/performance/shipping claims,
DHT or chat expansion, changes to original Java source, real profile or original
payload access, and arbitrary retries or zero-destination acceptance. The earlier
unexplained generated-file read exception stays open unless its cause is reproduced;
better diagnostics alone do not establish that cause.

Implementation `54af52933cc663634a6ad0ca2f6390ff38e34721`; test registration `ec948313479d6cb351cd8ed4724d551f5ff54f4e`; test-only correction `22916f704844836955cc33cce3a6aee86cfe6bcd`. [Actual qualification](../evidence/20261028-network-reliability/verification.json).
