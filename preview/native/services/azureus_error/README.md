# Original Azureus ERROR replies

This C++23 codec implements action 1032 from the pinned Java
`DHTUDPPacketReplyError` constructor and serializer. It depends only on the
qualified Azureus UDP header codec. Signed 32-bit unknown types consume their
type field and leave following bytes untouched; type 1 owns the reported
originator address; type 2 owns the blocked-key request and signature. The codec
applies no version-14 gate: the original host chooses when to send that error.
It performs no certificate verification, key blocking, routing, or address
admission.

An absent type-1 endpoint object is an input failure. An engaged unresolved
endpoint writes the original zero IPv4 address and zero port. Numeric IPv4 and
IPv6 fields never trigger DNS. A forced mapped IPv6 endpoint retains its 16-byte
wire form on output and normalizes to IPv4 on receive, matching the source
address factory. Receive width validation follows the mandatory port read.

The request length uses one unsigned byte; the signature length uses two.
Original byte-array reads perform one read and zero-pad any unavailable bytes.
A small incoming packet can therefore own a 65,535-byte padded signature. The
8192-byte full-packet cap limits incoming and outgoing wire bytes; it does not
change the decoded field semantics. An outgoing source-valid field that would
exceed that cap fails with `Limit`. Failed serialization returns its preceding
partial bytes only for diagnostics; callers must never send them. The separate
transport datagram limit remains outside this codec.

Decoded packets own the complete input, fields, offsets, and trailing bytes.
Copies are independent. Moves clear the source packet, body, and offsets.
Header failure codes and consumed offsets retain the qualified header behavior.
The bounds executable checks padded reads, primitive failure cursors, null and
unresolved inputs, unknown signed types, mapped addresses, whole-packet limits,
and copy/move lifetimes. Original-source fixture and fresh Java readback checks
are provided separately by the qualification harness.
