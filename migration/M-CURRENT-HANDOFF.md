# Current M — verified scope and stop boundary

The bounded four-stream I2P Streaming manager and explicit Known peer attachment are qualified. All required tests and the measured atomic promotion have completed. Further implementation stops at the user's request; the whole migration remains incomplete.

Implementation commit: `43d642fb0025510c581ebdc4d7a332752c511a46`. Qualified source checkpoint: `3d58f1c6f4bfe32ec4ea86aae748d39a4afdf742`. The following metadata commit records the actual result; its identity is available in Git history rather than invented inside its own content.

Evidence: [review](evidence/20261029-streaming-manager-known/REVIEW.md) and [verification](evidence/20261029-streaming-manager-known/verification.json).

Each of GCC, Clang and leak-enabled ASan/UBSan freshly passed the three affected suites: seven legacy original-manager controls and the four-application manager with genuine renewal, cancellation, real EOF and ACK across more than 125 seconds. Exact unchanged-image/definition associations retain 18 GCC / 16 common-lane rows. GCC Public additionally passed a fresh authenticated namespace run. Totals are 22/19/19 associated rows, not a fresh full-cohort invocation.

Five private subjects and the sixth authenticated Public continuation passed against the same candidate, with per-PID private Qt/provider initialization proof. This does not claim all 22 tests ran privately. Both hosted workflows passed for source3d58: [SDK](https://github.com/d00rsfan/BiglyBTC/actions/runs/40984645889) and [desktop](https://github.com/d00rsfan/BiglyBTC/actions/runs/40984645890).

Current dist manifest: `aad928753cb8f2dac8f99245298d987cfe2c4bee9e7bdebf5b8d7645ed6d559b`. Both registry validations passed; 20 upstreams / 108 components / 22 external dependencies and existing partial states are preserved. The superseded preview, six copied test images, test-support Qt library and namespace launch copies were removed after promotion.

The unchanged modified Java JAR has a confirmed EOF/output-close callback ordering race. The external Java fixture orders application close after the genuine FIN callback returns, retaining real EOF and actual ACK requirements. Earlier source/diagnostic/authentication failures remain failures in the evidence; general stability and performance parity are not claimed.

There are no open implementation questions or active local subjects for this milestone. Future authenticated network-test batching is recorded as a pending follow-up in MIGRATION_PLAN.md. No SDK/provider/DATA/chat chapter is started. The original Java checkout's ordinary HEAD/index, running client and live profile were not altered.

The plain GB-English reports are [empty-file v2/hybrid](reports/20261028-empty-v2-hybrid-bug.txt) (the same two previously reported issues) and [I2P close race](reports/20261030-i2p-fin-close-race.txt) (the new finding). The user will send reports personally.
