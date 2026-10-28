# P03 bounded same-profile local session

- State in_progress; owner Codex; self review only.
- Inputs: AD-001 local slice, AD-004 persistence, source-built Java partial profile,
  pinned native engine package and passing config/state/cache foundations.
- Source `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: GlobalManagerImpl,
  DownloadManagerStateImpl, RDResumeHandler, LinkFileMap and disk/file lifecycle.
- Writes native engine/app/tests, migration records and generated `/tmp` only.
- Never read/launch/interfere with the user's running BiglyBT or actual profile.

Build BiglyBTC's first actual headless local runner: read the existing Java
inventory/state/file mapping, acquire the shared lease, take state/cache ownership,
continue the same incomplete payload from an explicitly owned loopback peer,
flush/close the engine, and write Java-compatible verified piece/completion state
without changing info hashes or unknown fields. Preserve original settings/ports.
Reopen the same generated profile in original Java and verify complete content.

Initially scope the executable to explicit experimental local single-file linear
workloads, rejecting other capabilities before payload writes. This is a bounded
vertical slice, not P03 completion or permission to drop multifile/storage/network
features. Expand those through their own original-source cases. No public
discovery/router traffic or permanent Java runtime in the native executable.
