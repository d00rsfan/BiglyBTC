# Signed Azureus key-block verification and saved state

Native source `db772bb57b935d3055be10dd04486b471dd34425`, repository tree `3483229a7b3bf8c4f7c71ef0c5a3fc840201be2a`; original baseline
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Pinned official RSA modulus/exponent and MD5withRSA PKCS1-v1.5 verification use
OpenSSL3 EVP. Explicit generated-key protocol primitives support tests only;
normal owners use the fixed official verifier, with no saved/wire trust-root
switch. Three actual original signature results match Native. No valid fixture
signed by the official private key is available, so this does not claim such a
positive original-authority proof. Unavailable crypto is distinct from an
invalid signature, preserving original failure-filter exception behaviour.

Actual original DHTPluginStorageManager methods produce40 precedence cases.
Native owning state matches direct/indirect priority, signed creation time,
add/removal, duplicate add and same-time decisions. Original duplicate add may
return an unverified new object without replacing the stored block. Native
matches its returned-block boolean but exposes only the existing trusted stored
record; unverified replacement metadata gains no trust. Failed verifies use
the already qualified common original add-only Bloom4000/strict30minute reset;
other originators, wall-clock rollback and crypto-unavailable retries are
checked. Original Bloom500 sent-contact knowledge is recorded on accepted
replacement/explicit acknowledgement; direct entries purge on local-ID change
without removing indirect entries. These pure services confer no socket/source
permission or durable-save acknowledgement by themselves.

Actual original fixed-clock readKeyBlocks loads36 generated saved files.
Native matches direct-add retention, seven-day expiry of other records, exact
expiry, future receipt clamp, direct==1 and Java int truncation/overflow. Pure
worker-side saved codecs preserve opaque root and accepted-entry fields.
Malformed typed areas refuse replacement rather than silently discarding their
existing bytes; this conservative unsupported-input policy is outside healthy
original parity. Signature-invalid or expired healthy entries are skipped.
State snapshots are owning; restores are validated before replacing healthy
state. Limits are8192 records/4MiB record bytes plus existing finite wire/core
bounds; overflow refuses instead of truncating live state. No filesystem I/O is
performed by the service.

GCC16 Release/C++23/Qt6.12.0 focused build and wire/policy/state suites pass.
Original generated-key substitution and fixed-clock substitution occur only in
isolated short-lived JVM helpers and are restored; their owned generated files
are removed after extraction. The first Native state compile found a missing
transparent comparator and was corrected before accepted checks. No full Host
rebuild, real profile, public-network run, daily image or full F09 completion is
claimed. Actual owner/source/checkpoint, request/error serving and token-first
forwarding remain next. Detailed group counts are in qualification.json.
