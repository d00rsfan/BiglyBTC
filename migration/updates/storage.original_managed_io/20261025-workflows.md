# Original storage workflow qualification

Upstream baseline/review: `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.
Native implementation: `2b982ac35f8458f98b194cc4ac0d2ee9adbc36d5` (d00rsfan/BiglyBTC main).
No upstream baseline advancement; this is a further bounded port.

Active priority/DND edits, explicit stopped L→C, allocated-tail preservation,
unverified fully-present normalization and nonlinear Move are implemented.
Three compiler/sanitizer lanes each pass15 affected suites, actual Java compact
and C/R/X Move/partial/full handoffs pass, and hosted/private runtime pass.
See ../../evidence/20261025-storage-workflows/REVIEW.md and verification.json
for exact receipts and limitations. Whole components remain in_progress.

Source-only independent reviews: compaction replay by error_driver; allocation
arithmetic by error_codec; stale/reentrant GUI/Host gates by error_oracle.
Root owns actual compilation, original-class execution and publication.
Remaining EOF-short/other layout corners, v2/hybrid, broader shared/merge/bulk
workflows and the existing counter vptr exception retain their open tasks.
