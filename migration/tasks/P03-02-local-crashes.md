# P03 local same-profile crash recovery

- State in_progress; owner Codex; self review only.
- Parent P03-01 local session; F05.linear_handoff_recovery.
- Original source baseline `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`:
  DiskManagerImpl.setPieceDone completion links, LinkFileMap, RDResumeHandler,
  DownloadManagerStateImpl and GlobalManagerImpl persistence.
- Writes native engine, fixture harness/tests and generated `/tmp` roots only.

The normal v1/v2/hybrid Java -> native -> Java handoffs now pass GCC. Interrupt
the owned native process after state preparation, checking, disk flush,
completion link, completion state and inventory save. Every prior or new metadata
view must retain a readable name and conservative piece flags. Resume the same
files in native and original Java, proving correct bytes and piece ownership.
Preserve unknown fields/settings/policies and unrelated collision targets.

Completion currently retains the old name until the new state and inventory are
durable. Add a bounded versioned intent in the existing opaque attributes map so
native can finish alias cleanup after an interrupted rename even if Java opened
the profile in between. Java must read and retain that extension without losing
its own state. Verify inode identity before deleting only the recorded alias.
Do not delete an unrecognized file, overwrite a collision target or publish
DONE flags before disk flush. Native-only temporary metadata is never required
for Java to read either valid payload name. Power-loss and other platform/layout
qualification remain separate gates.
