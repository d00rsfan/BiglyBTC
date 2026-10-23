# Original Azureus FIND_VALUE packets

C++23 full packet codec pinned to original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. It supports subsequent original DHT
lookup/storage services for ratings/message-sync, with no ordinary managers,
profile, DNS, network listener, routing, DB writes or plugin activation.

Requests preserve0..64-byte targets, body flags independent from generic header
flags, and the source maximum-values setter: only values>255 clamp, negatives
wrap low8, getter unsigned. Partial targets/required fields and null/oversized
encoder inputs retain source order and global header skew/cursor diagnostics.

Replies distinguish null values/contact mode from empty value arrays. At>=6,
continuation and diversification are present; earlier continuation defaultsfalse
and diversification defaults DT_NONE1. Any nonzero wire boolean is true, and
unknown/signed diversification bytes remain intact. Value replies serialize with
negative supplied remote clock skew (LONG_MIN negation wraps) and decode with
zero skew. Contact replies parse the ordinary contact array first, filtering port0,
then Vivaldi only at>=16. Position provider rewind/failure and cache side effects
are retained. There is no upload block in these packets.

Headers, targets, values, positions, contact IDs and raw tails own storage; moved
packets clear old offsets/body. Incoming endpoints remain inert. Whole packets
are capped at8192; transport1400 budgeting is separate. Failed partial encodings
are diagnostics, never sendable. Value arrays reuse the prefix reader without
unused-tail copies. Source array/count/storage/routing validation remains outside
this codec.

The guarded reference copies exact original full constructors/serializers,
max-values setter/getter and reply getClockSkew. A supplied inert contact subclass
provides remote skew to the original base constructor. Nested types/exceptions
adapt namespaces only; unchanged original helpers/frozen classes and ReferenceFence
remain verified. Fixtures contain5294 source observations. Native tests cover
flags/max fields, continuation/noncanonical booleans, skew wrapping, mode/null
ordering, provider fallback/rewind, ownership, limits and crypto/thread failure.

Continuation-aware delivery (each part followed by final/error), actual token
authority, lookup/routing/storage/persistence and host/plugin activation remain.
This is not an operational DHT, complete client/plugin release or measured
whole-client performance claim.
