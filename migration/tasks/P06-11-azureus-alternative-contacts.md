# P06-11 Original Azureus alternative-contact wire and identity

Status: implemented bounded wire component, qualified locally; full host/transport
integration pending. Pin core cb8c6cb9bb97916cbc7e80338a017200ffc3e71f before
coding. Complete the next PING reply segment: contact-array framing, unsigned
network/version getters, signed age normalization, monotonic seconds wrapping,
raw property ownership and Arrays.hashCode identity. Source helper uses exact
wire/constructor/getter bodies and a supplied clock; no normal DHT/core/plugin/
configuration startup, registration, DNS/network or live profile.

Outgoing host snapshots supply already encoded property dictionaries. Qualify
their field framing against the original serializer and actual original BEncoder
on generated valid maps. Incoming raw properties stay opaque, including original
single-read zero padding on truncation. Lazy getProperties BDecoder tolerance,
canonical projection, endpoints/overlays and lifecycle/routing remain separate
host work; do not claim opaque bytes provide a complete property-map adapter.

Bound packet input/output at8192 bytes. Original encoded field length is65535;
one truncated field can expand into at most65535 bytes, with total successful
owned property bytes bounded by8192+65535. Characterize zero/full/over counts,
partial short/length reads, padded property input, signed bytes/hash wrap, negative
ages, clock wrapping and tail offsets. The PING reply's separate16-contact list
selection policy remains assembly work; array helper accepts up to64 contacts.

Qualify fixtures, ownership/bounds, Java readback and GCC16/Clang24/sanitizers with
leak detection enabled. Record exact source/native/class/fixture/binary hashes.
Then assemble complete PING packets and test the owned-loopback UDP actor. No
operational DHT, plugin/runtime activation or release completion is claimed.

Result:5636 original observations, two CTest rows,2506 original encoder
comparisons and2509 Java readbacks/compiler pass on fresh GCC16/Clang24/
ASan+UBSan+LSan. Evidence: migration/evidence/P06-11-azureus-contacts. Continue
full PING assembly and owned-loopback transactions while lazy property behavior
remains explicitly separate.
