

## 2026-11-06 measured original-manager acceptance

The previously outstanding actual inbound gate passed6 Native and14 original commands on implementation7b3e55 / launch a95bc8 / Native subtreefa98. Exact full identities and independent evidence are in ../evidence/20261106-original-i2p-inbound/REVIEW.md. Bad-token exact peer readback, corrected accounted counter interval, stop/resume, actual source revocation, joins/reacquisition and clean isolated shutdown are proven. Earlier consumed failures/freezes above are historical. Remaining receive-only expiry and intentional router/I2CP EOF are not credited; maintenance/overlay/privacy/P06 requirements still keep P07 open. Continue the user-resumed full plan.

## 2026-11-06 receive-TTL expiry and owned-router EOF acceptance

Implementation `00e2e70a58bf0a0d73f548c0cf785779b5792f55`, Native subtree `d85be38ad6bea02eb010f1f9cf7f741ae9f7dc6a`: actual fresh GCC/SAN receive-expiry-only5/4 each and independent GCC owned-router EOF3/3 passed, with final joins/locks/cleanup/fences and independent review. [Evidence](../evidence/20261106-i2p-receive-lifecycle/REVIEW.md). Counter0→1 is actual per-scope TTL removal while input remains queued; source/session/scope and fresh ping recovery stay current. EOF is exact owned router A SIGKILL/-9 and first armed TransportClosed, with source authority current and surviving subjects joined. Prior6/14 and magnet proofs are retained separately; failed global-empty attempt remains FAIL. Core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`, official I2P `9134f808337b401e8e53c73734c81fab04280c9d`, router SHA256 `b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5`, SDK SHA256 `dfe077249c82e550cd0fd82cd7bfb14841a303bba17c90b74e7d9025b7ce14d3`. Whole P07, dependent overlay/P06 work, P08 and global baselines remain open. The prepared P08 candidate is not adopted here.

