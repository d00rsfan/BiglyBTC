# Original Azureus alternative-contact framing

C++23 array/contact wire fields and original age/duplicate-detection hash at
core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This is the next PING reply
segment for the shared original DHT. It starts no Java/core/plugin manager,
listener, discovery, DNS or profile access and has no JVM runtime dependency.

`encodeContacts` borrows supplied snapshots with already encoded property
dictionaries. It performs original count/field/length framing, low-byte/short
narrowing and bounded owned output. It does not validate a dictionary or perform
the original lazy property-map decode/canonical projection. The host must use a
qualified property encoder. Original BEncoder comparisons cover generated valid
maps only. Failed partial bytes are diagnostic and must never be sent.

`decodeContacts` owns raw property bytes, fields and trailing data, returning the
exact consumed offset. It preserves the source's single-read behavior: missing
property bytes remain zero instead of causing a fixed-field EOF error. Raw
properties stay opaque and are never interpreted as endpoints or overlay settings.
The weak signed-byte Arrays.hashCode value is for duplicate detection, not
authentication. Negative wire ages clamp to 32767 before monotonic-seconds/int32
wrapping; later age queries preserve source wrapping and truncation semantics.

Copying retains independent bytes and metadata; moves clear the old identity
owner. Input/output bodies are limited to 8192 bytes. The original property length
is 65535, so a bounded short read can expand a field to that size. Total owned
properties are explicitly limited to 8192+65535 bytes before allocation. The
array helper accepts 64 contacts; the PING reply's separate list-selection limit
of 16 still belongs to packet assembly. The enclosing transport must enforce its
complete header/body packet budget and dispatch policy.

CTest compares 5,636 original observations and checks limits, partial field reads,
padded expansion, hash/clock/age wrapping, copy/move/input ownership and tails.
Migration qualification also compares 2,506 original encoder results and 2,509
Java readbacks per compiler, including 512 fresh generated maps and three padded
expansion cases. The helper copies exact original wire/constructor/getter bodies,
supplies a clock and uses unchanged frozen BEncoder/BDecoder for outgoing maps.
It does not call the incoming contact's lazy getProperties method. Original
classes, methods, source, fixtures, logs and binaries have recorded hashes.

Lazy property decoding/canonical projection, endpoint/overlay integration, reply
selection and full PING assembly, owned-loopback transactions, contact/routing
lifecycle, storage and plugin/runtime activation remain required. This codec is
not an operational DHT or a full plugin release.
