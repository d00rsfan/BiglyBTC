# Azureus query-storage and explicit replication survey dependency

Continue the cache-maintenance chapter without a daily image. Original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; source commit coverage is registered
before implementation. First qualify query-storage request/reply wire format
with actual original Java classes and the six-byte-prefix/two-byte-suffix DB
lookup, including collision and deletion behaviour. Preserve the original
reply bitmap wire length (size+7/8 evaluates to size). Reject truncated or
oversized inputs before DB effects. This does not grant routing or STORE tokens.

Then connect bounded requests/replies to the actual owned actor with ordinary
endpoint/transaction/source gates, receipts and expiry. Only after this dependency
is qualified port original explicit replication survey decisions and scheduling.
Keep original all-explicit values out of default cache forwarding. Signed key
blocks and the rest of F09 remain separate unfinished work. No live-profile
changes, new daily build, agents or repeated unchanged green application builds.

Wire/DB prerequisite qualified in `90aa3c49107c6a6c9e8f5fc53a18195b8c31e31f`; [review](../evidence/20261115-azureus-query-storage-foundation/REVIEW.md). Continue owned actor and survey integration.

Owned sender/receiver and actual normal-profile broker qualified in `60d1384b63fb6645dc44b6173be2d095177ccf7a`; [review](../evidence/20261115-azureus-query-storage-runtime/REVIEW.md). Continue survey batching and replication decisions.

Pure eligibility/query batching prerequisite qualified in `24cec5ff4b3c65fa80815cf185a10d0a3ca7881a`; [review](../evidence/20261115-azureus-survey-eligibility/REVIEW.md). Continue actual survey integration.
