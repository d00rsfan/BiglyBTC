# Build and temporary-file policy

User instruction, 2026-10-30: remove obsolete garbage builds; do not accumulate temporary files. Disk exhaustion interrupted qualification. A legacy sanitised build alone occupied12GiB.

Reuse shared build directories for each necessary configuration. Do not create a new compilation cache for each milestone. Keep only current dependency installs, current preview and builds needed by active work. Delete obsolete generated objects/binaries after checks; do not archive or copy them for rollback. Source, Git history and compact committed test receipts are the durable record.

Check available disk space before expensive builds and router fixtures. Require at least5GiB free; stop before consuming the reserve. Delete owned runtime profiles/payloads after subjects join. Keep diagnostic output only when needed to explain failures or substantiate qualification; never copy entire build trees or caches into evidence.

A broad recursive removal of236 old build directories was rejected by automatic approval review. Cleanup may instead use a proven generated-file scope, preserving source, active processes and dependency/runtime inputs. Do not bypass that rejection.
