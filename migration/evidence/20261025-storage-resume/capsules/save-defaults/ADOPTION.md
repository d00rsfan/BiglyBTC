# Save roots and original storage defaults

Source-only batch against the exact beforeimages in `before/`; no builds or
runtime/Java equality credit. Seven source paths are staged. Apply the two
production callsite patches, not whole-file replacements: the separate frozen
negative-filelinks parser patches touch the same files and must be composed.
The large-piece worker owns the shared layout/codec bounds; these are untouched.

New shared adapter: `original_storage_defaults.hpp/.cpp`. `resolveOriginalSaveRoot`
returns a copied canonical SavedDownload; no inventory/payload is changed by the
resolver. `restoreOriginalSaveRoot` validates a journal's captured root against
the original possible interpretations without repeating post-move existence
decisions. Explicit roots still use authenticated original filesystem mappings.

`resolveOriginalStorageTypes` distinguishes requested policy from physical
layout. Missing/empty lists use held `biglybt.config`: source defaults are reorder
false / 10 MiB; nonnegative saved `reordermb` overrides the global threshold.
Wrong types/out-of-Java-int values and present unreadable settings fail closed.
Existing C/.2 primary/backup markers are owned, bounded fixed leaves; unsafe or
pending candidates and simultaneous control families refuse. Existing packed
data cannot become a blind L default. Unsupported cross-family conversion is
reported separately; file operations can preserve/move actual bytes, while
ManagedStorage refuses admission requiring that original-unsupported conversion.
This does not certify missing-control guessed permutations.

Managed preparation rechecks default/marker identities, journals any C→L through
the existing path, persists requested default policy/threshold, and migrates only
the selected inventory save_dir/save_file before creating payload parents.
Legacy `path` and opaque row/root values remain untouched; the migration uses
the original ConfigFile raw baseline/CAS. Checkpoint retains the canonical
captured mapping and current user-command fields.

File-operation preparation stays read-only. Captured requested/physical kinds
are included in the existing approved scope/journal. Recovery validates kinds
against the original policy and exact captured indexed control inventory; it
does not reread mutable global defaults or infer root type after a move. Modern
and legacy dictionary inventories retain their form, unrelated rows, mixed
pauses and sparse recovery merge behavior. Control membership validation uses
an indexed map rather than files×controls scanning.

Root CMake actions (no CMake files edited here):

```cmake
target_sources(biglybtc_profile PRIVATE original_storage_defaults.cpp)
add_executable(biglybtc-original-storage-defaults-tests original_storage_defaults_tests.cpp)
target_link_libraries(biglybtc-original-storage-defaults-tests PRIVATE biglybtc_profile)
target_compile_options(biglybtc-original-storage-defaults-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME original-storage-defaults COMMAND biglybtc-original-storage-defaults-tests)
set_tests_properties(original-storage-defaults PROPERTIES TIMEOUT 120)
```

The new plain C++ test has twelve authored families, not executed results.
Existing ManagedStorage bodies are unchanged with three added families;
existing eight file-operation families are unchanged with one added family.
Run the new target plus managed-storage/file-operations in the central batch.
Root controls actual compiler/runtime/original and portable qualification.

Intentional bounds: normalized absolute owned/no-follow payload policy remains;
opaque deprecated filelinks is not interpreted; unsupported arbitrary store
tokens are not silently coerced to L. Legacy charset handling, stopped L→C and
active file-policy changes are separate remaining tasks.
