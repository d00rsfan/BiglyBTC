# Exact original file-link migration

Source-only capsule. No compiler, Java/native test, client, profile or network
execution was performed. CF-02 remains separate and unadopted.

Adopt the three new files under `staging/preview/native`, append the core source
and plain test registration from `Targets.cmake`, and replace only the two
`filelinks2` parser blocks using the patches. **Do not replace entire call-site
files over the independent save-root/default-storage changes.** Full before and
candidate files provide exact byte pins; surrounding save-root/default logic is
intentionally unchanged here. The caller must first use the qualified resolved
save root from the companion `save-defaults` capsule.

`compat::resolveOriginalFileLinks(Node*, span<const string> bases)` returns owned
optional links by original file index plus ignored-row diagnostics. Bases are
the exact absolute original paths, computed before applying any link:
`resolved.saveDirectory / (multifile ? resolved.saveFile/file.path : resolved.saveFile)`.
No directory scan, link chaining, canonicalization, filename conversion or
filesystem access occurs in this pure helper. The raw document remains intact.

The pinned original `LinkFileMap.fromBencodeObject` trims the signed int32 index,
discards trailing empty split fields, accepts an absent target as a removal,
ignores fields after the third, and applies rows in order. Any negative index
resolves its raw `from` against `getFile(false).getAbsolutePath()`; an unresolved
row leaves earlier mappings alone. Nonnegative indices do not validate `from`.
`put` removes an entry when target is absent or nonempty `from` equals target;
otherwise the last row wins. `FileKey(String)` preserves empty components, so
raw equality must not be replaced by filesystem normalization. A present empty
target followed by another field remains an unsafe link, rejected by callers.
Non-string list members are filtered by the original state attribute reader.

Native safety bounds: 100,000 files, 400,000 rows, 16 KiB per row, 64 MiB total
row bytes and separately 64 MiB total base bytes, absolute UTF-8 base paths up
to 4096 bytes. Malformed UTF-8 is refused rather than guessing Java replacement
filenames. Index spelling admits the original writer's ASCII signed decimal,
including plus, zero padding, `-0`, INT_MIN and original trim code units; other
numeral scripts are diagnosed/ignored. Positive orphan indices are diagnosed
and ignored because no metadata file can query them. Ambiguous exact negative
base matches fail closed; original would take its first match. Resource and
ambiguous-path refusals grant no broader original-format parity claim.

Both call sites still validate selected targets and retain their no-follow
descriptor admission, lexical/UTF-8/component checks, alias and inode refusal,
profile/reserved-path exclusion, other-torrent ownership, raw CAS, journal and
recovery guards. An indexed row's obsolete `from` is not payload authority.
Missing/unresolved negative rows never authorize a guessed alternate payload.
An explicit later transaction may emit ordinary indexed links as before; this
read-only projection itself does not rewrite partially migrated rows.

`original_file_links_tests.cpp` covers exact negative paths, signed boundaries,
obsolete indexed sources, mixed overwrite/reset order, unresolved/bad rows,
Java trailing-empty semantics, null/empty sources and targets, extra fields,
FileKey raw equality, ambiguity, encoding/size guards and owned result lifetime.
Two short `.inc` fragments extend the existing storage/file-operation suites
with real authenticated external payloads, a rejected chained migration and a
held-profile authority refusal. Root owns merging and executing them.

Original subjects and exact native/test before pins are in `source-pins.json`;
`callsite-pins.json` records full source before/after hashes. The source-only
reversal check is recorded in `static-checks.json`; it is not test credit.
