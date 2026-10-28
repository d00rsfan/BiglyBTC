# Original Tor SAFECOOKIE proof review

Self review, 2026-10-17. Original aznettor
`ae8efb8d40fb38246ac1790ab5cfb11584689c4a`, exact
TorPlugin.ControlConnection.connect block from AUTHCHALLENGE through AUTHENTICATE,
ending before TAKEOWNERSHIP. Full source/block/harness hashes are in fixtures.

Native offline functions generate the uppercase challenge command, validate the
standard ordered SERVERHASH/SERVERNONCE reply, compare the server HMAC-SHA256
proof with CRYPTO_memcmp, and only then return the uppercase controller proof.
OpenSSL3.5.5 uses EVP_Q_mac; cookie-containing scratch bytes are cleansed. Returned
command lifetime belongs to the caller; this code logs nothing. Input cookie and
client/server nonces must be32 bytes, replies bounded1024 and single-line.
There is no authentication or direct-connection fallback.

Original block runs unchanged in a fenced helper with supplied nonce/cookie and
transcripts; FileUtil and sendAndReceive are controlled stubs with no file or
socket operations. Python independently creates128 deterministic server proofs
and eight transcript variants each.384 valid and640 failed original outcomes,
including complete emitted commands, match on GCC16, Clang24 and Clang24
ASan/UBSan/LSan. Additional tests reject every changed server-proof digit, wrong
cookie/nonce length, multiline injection, oversized input and unknown extra fields.
OpenSSL header/runtime and actual native binary hashes are pinned in matrix.json.
Leak detection remains enabled; no Tor process or connection ran.

This is not an operational plugin. Native parsing intentionally rejects malformed
hex/field lengths or extra fields that original substring parsing might accept;
only the stated standard transcript scope is qualified. Cryptographic nonce
creation, original cookie/onion identity handling, command framing/events,
connection deadlines/cancellation, process ownership/idle-stop, settings/UI and
proxy privacy integration remain pending. No public source/binary release is
claimed. GPL2-or-later source retains original Azureus Software copyright and
ships the full GPL2 notice; OpenSSL package notice is retained separately.
