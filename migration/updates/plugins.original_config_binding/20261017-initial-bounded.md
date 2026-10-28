# Original plugin configuration binding review

Self review, 2026-10-17. Original core baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`, mlDHT wrapper
`3b12515c5541ed75cfb4f795b672d4e786df9f45`.

A bounded read-only view preserves the distinction between runtime ID, external
plugin-directory basename, folder-derived configuration prefix and runtime-ID
startup flag. It reads original Long values with signed int32 truncation and
nonzero boolean semantics, preserving wrong-type fallback diagnostics. mlDHT
settings retain the original defaults and exact key spelling. Default BDecoder
uses ISO-8859-1 keys; native matches that lookup, including the original inability
to find some non-ASCII keys saved by the UTF-8 writer. No key repair occurs.

The fenced Java helper uses exact getter/prefix/user-file method bodies and
controlled properties/defaults, frozen original BEncoder/BDecoder, and no normal
core/plugin initialization.90 observations cover six directory names, encodings,
legacy raw Latin1 keys, wrong types, numeric overflow, disabled flags, port range
and unrelated keys. The original70 observations are retained and identical within
the expanded90; the initial native UTF-8 lookup failure is retained, not hidden.

GCC16/Clang24/ASan+UBSan+LSan each pass the90 original setting snapshots plus
routing-cache and document-edit regressions. Matrix/source-verification capsules
record exact commands, source and binary hashes. No full application matrix,
plugin startup, disk edit or network behavior is claimed. Folder/runtime binding
rejects unsafe basenames; Linux generated-profile scope only. Production host
lifecycle, settings mutation/default registration and data discovery remain open.
