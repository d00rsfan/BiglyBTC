# Keep profile I/O off the DHT network owner

State: open; required before public activation and performance qualification.

Current source6726a4cf7b017ac31d6aeede15f339c3ec2b71f7 has a concrete limitation: normal STORE authority calls AzureusValueVersions::validate() on its network actor thread. That method captures version.dat and version.saving bytes; next() also reserves/fsyncs original version ranges synchronously. Current explicit loopback fixtures prove compatibility and lifecycle, not this architecture/performance gate.

Move profile preflight and durable reservation to a bounded I/O executor retaining the same profile OFD. Deliver immutable request/generation/profile-bound results to the network owner. Preserve exact original filenames/unknown fields, CAS/conflict and indeterminate-publication semantics, no version before durable reservation, cancellation/stop fences and actual socket/router/DB authority. Neither stale I/O completion nor an old permit may advance a new owner/request. Do not claim protection against an atomic hostile pathname swap that the present cooperative-lock contract cannot provide.

Verify network deadlines/shutdown can progress during filesystem work, a replaced profile/source still refuses, late/cancelled completions cannot send/store, and Java/native version-file compatibility remains unchanged. Reuse relevant existing fixture/fault coverage; do not replay unrelated Java/engine suites or add a general-purpose framework. Record actual resource/latency evidence before closing this task.
