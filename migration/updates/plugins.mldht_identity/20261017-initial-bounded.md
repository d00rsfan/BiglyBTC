# Initial bounded mlDHT identity import review

Self review, 2026-10-17. Audited original engine fork baseline
`f9290cbd63805229aef418639b781696c5fa1573`. Node.initKey and Node.persistKey bodies
also match the wrapper POM's original engine
`067f434eb60323123a5e885093ab2504ab10d5f9` byte-for-byte; body digests are retained
in P05-02/identity-source-bodies.json. This does not prove shipped engine pairing.

Read-only import implements Files.readAllLines UTF-8 validation before selection,
Java ASCII trim, CR/LF/CRLF, Pattern.asPredicate substring search followed by the
40-character Key constructor, first sequential selected key, and original uppercase
Linux writer encoding. The returned value retains original bytes. A1MiB import
bound rejects excessive data without writing. No new random identity is generated.

Original fixture harness uses exact Node init/persist method bodies and four
unchanged original Key/Prefix/Arrays/Radixable sources. DHT configuration/storage
and deterministic random fallback are explicit supplied host boundaries; no DHT,
plugin or application starts. ReferenceFence limits filesystem access to generated
roots and prevents network/process side effects. Frozen class/source/method/harness
digests protect reuse of the original helper.

117 original observations cover104 restored identities and13 regenerated corrupt
inputs, including control-character trim, mixed case/newlines, Unicode comments,
BOM/NBSP, invalid UTF-8, overlong hexadecimal substrings and trailing bad encoding.
Native matches104 selected keys and actual original writer digests. For13 original
fallback cases it explicitly rejects import and preserves bytes/inodes, following
the non-destructive persistence gate; automatic identity replacement remains a
separate unimplemented operation. No whole-plugin integration hash is advanced.

GCC16/Clang24/ASan+UBSan+LSan each pass both identity and4860 host-policy regression
suites. Each compiler also performs104 native-writer -> fenced original Java
init/persist roundtrips with identical keys and bytes. Exact commands, source and
binary hashes are in P05-02/identity-matrix.json and the three roundtrip records.
No sanitizer suppression or disabled leak detection was used.

Identity engine-derived files retain MPL2 notices separately from GPL2-or-later
wrapper policy. Actual file-writer/profile lease integration, secure key creation,
routing cache/node-ID derivation, original restore-disabled settings, scheduling,
wire/engine pairing and full network/privacy policy remain required.

Clean-export replay initially failed because Git core.autocrlf=input normalized
the CRLF identity fixture despite matching working-file qualification. An audit
found the same archival issue in one older scheduler ownership input. New fixture
attributes preserve all golden bytes; checkpoint validation now rejects any
compiled/fixture blob differing from the working input. Historical commits are
retained unchanged and the archival correction is recorded separately. No live
profile was read or modified.

A second clean export at a shallow /tmp checkout exposed a test-helper path-depth
assumption. Fixture discovery now checks local exported fixtures first, with a
workspace fallback that is safe at any directory depth. Both CTest suites pass
on identical GCC/Clang/sanitizer binaries and the shallow clean export preview;
fixture-location-followup.json records the final helper hashes and replay scope.
The earlier failed export logs remain retained; neither failure was published.
