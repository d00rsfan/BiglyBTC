# P02-10 SpeedScheduler standalone native source and hosted CI

State: standalone source export is locally and hosted qualified, published/tagged.
Whole operational plugin/client integration remains unfinished. Historical checkpoints
below retain their original pending states. Public README bootstrap is
34b27c00f36f56f2b0ac242f137266f5c89e0ce4 at durable sibling
/home/yurii/Java/BiglyBTC-plugin-SpeedScheduler. Source authority original plugin
1ee6a53dae017696d2f0002e4a0d74f5a9476af5 and BiglyBT
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Native module implementation first
appeared at private afeb3b2a25e6f37ac27e53ad8fed5d2fa426d671. A current immutable
byte-exact checkpoint, not that old fixture blob image, will supply this export.

Root approves minimal standalone preparation: prefer local Toolchain with existing
monorepo fallback; four fixture-directory selectors gain standalone fallback and
otherwise keep exact replay ASTs/checks/timeouts; add scheduler export kind and
standard hosted Ubuntu26.04 GCC16 Release + ASan/UBSan Debug recipe. Pin existing
published SDK eefbd205ca0940ad4868ee07989b2d42be1a99dc (13files independently
match native); exact Qt6.12.0 Core bootstrap unchanged. Six existing scheduler
suites and two SDK suites run unfiltered per lane. No Core package/JVM/engine/live
scheduler activation or unrelated plugin-matrix dependencies.

Source freeze V1manifest5045444453ed07a1b4736a292145490f3f12a54cab6d4bd5c2cfe9fa36db86b8;
followup V2manifest0945d5ecbfab86890a208e4866b40e147f8d5984946b16e3a969a653e2af6b24.
Independent source review2761c714048a98d5eca3629d5f13eb95e5477213ec221f9b63c8bce8b1f46b1e
and independent input audit03debd1fee899971cce3c7e77d19a7f1382a0dd27fccea388098baba6ac87393
find no blocker. Root-reviewed sibling exporter/verifier allow exact corresponding
known sibling only, retain generated/tmp validation support and existing clean,
HEAD/provenance/origin/CI guards; exact authorized HTTPS/SSH origins accepted.
Fixture subtree attributes explicitly preserve bytes on Git export/commit.

All74 generated fixture files/1886391bytes match their frozen manifests and private
ae10147424936506bd978bb2f8226d5c505c92c0. The historical afeb ownership CRLF file
was Git-normalized110bytes, while qualified raw is113bytes; raw-preserving fix
015241e1ea32c6fa8b0f191718efb9f6983aee02 remains explicit. Never normalize/refreeze
fixtures or label old afeb exact export authority. No original Java trees/JARs or
user XML/pause state exported. Native attribution notes the actual original source
and license provenance; user's existing public-native-port authorization applies,
without a new approval request solely from old missing-license notes.

Next root gates: private checkpoint preserving original Java HEAD/index, source
export into existing clean durable sibling, exact working+committed blob/hash
verification (including CRLF), local SDK2+scheduler6 GCC/sanitizer builds, then
source commit/push and actual hosted two-lane results/artifacts. Preserve failures;
no changing assertions/fixtures/timeouts to obtain green. Record actual private,
public source/workflow/metadata hashes after they exist. This exports a bounded
module/policy foundation, not whole operational plugin/client or performance parity.

Private immutable export authority is6f3c5f29938da958031173805ce9cf8ae15445f1,
created with isolated index/raw native+fixture blob verification; original Java
HEAD/index unchanged. Exact clean bootstrap/remote34b27 was rechecked; source
export prepared directly in durable sibling (no new/tmpworkingclone). All96
committed Git files byte-verify,95manifest members plus provenance; CRLF113bytes
preserved. Public implementation/source commit91c417ca5aa5494f8c5791d8f29cf96350a0071d
is now pushed. Exact verifier receipt is committed-export-verification.json.

Fresh SDK+module builds from exportedsource use localSDK tree identical to pinned
publiceefbd205. GCC16 Release and GCC16 Debug ASan/UBSan/LSan each pass all2SDK
and6scheduler CTests, zero skips. Module and policy both replay6336originalstates,
XML/time and10ownershiphelperfixtures pass. No algorithms/fixtures/timeouts weakened.
Initial LSan tests inside traced sandbox stopped before testing with explicit
ptraceunsupported diagnostics; exact logs preserved. Same binaries/tests outside
tracing, leak detection stillenabled, pass. See local-verification.json and local/.
Hosted workflow39125597839 is running/queued; no hosted qualification credited yet.

First hosted workflow39125597839 now passes bothmatrix jobs. Downloaded artifacts
independently confirm12scheduler+4SDK CTest rows total, zero skips, exact6f3c5f
source/eefbd205SDK provenance and6336-state actual module replay. Evidence is
hosted-verification.json; no nativefix was required for the fresh standalone matrix.
A documentation-only followup now removes the stale publication-pending README
line using the actual91c417 firstsource commit. Source/policy/fixtures/workflow
remain unchanged; re-export will record its new immutable docsource authority.

Final documentation source30b645e44f8e58829064c26b35694110e81fd222 is pushed,
only README and generated source-authority metadata changed from qualified91c417.
All96 finalcommitted Git files verify rawexact; code/fixtures/workflow unchanged.
Its automatically triggered hostedrun39126062262 also succeeds. Annotated public
tag milestone-native-scheduler-source-20261018 is pushed. Full actual hashes,
local/hosted results and unfinished runtime scope are in publication.json.
This closes only this bounded source-export/standalone CI task, not the scheduler
policy/module broader integration gates or whole-plugin parity.
