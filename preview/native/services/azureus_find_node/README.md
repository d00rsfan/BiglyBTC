# Original Azureus FIND_NODE packets

C++23 one-stream packet codec pinned to original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This is shared groundwork for the
original DHT used by ratings, message-sync and related content. It does not start
transport, perform DNS, follow returned endpoints, initialize managers/plugins or
read profiles. Mainline DHT is a distinct protocol.

Requests preserve0..64-byte targets, signed status/size at version22, and BiglyBT
network4 upload blocks at55. Missing interim upload blocks are accepted; malformed
optional blocks retain source consumed offsets. The source single byte-array
read pads unreceived target bytes with zero; later required integers still fail
at EOF. Null/oversized outgoing targets fail after the already serialized header.
The wire target is not restricted to a20-byte node ID by this codec.

Replies preserve anti-spoof random ID at7, status at12 (default-1), estimated size
at13, position framing at10, and regular UDP contact arrays at every version.
There is no PING first16 selection. Header negotiation, signed-byte comparisons,
minimum/network/action checks, position reset, skew and global failure cursors
compose qualified existing primitives in source order. Supplied random IDs are
wire values; no anti-spoof authorization or nonce lifecycle is implemented here.

Decoded packet/header/body/contact/target values own their storage, and raw tails
are computed from the owning packet. Moves clear the previous owner's offsets
and body. A caller supplies one owner-thread NodeCache for contact IDs; it is
borrowed only during decoding. Port0 records hash before filtering, and later
failures retain prior cache effects as in the source. OpenSSL/provider and wrong
cache-thread errors remain explicit. Returned contacts never register themselves
or become network destinations.

The whole packet cap is8192 bytes. Partial encoder outputs are diagnostics and
must never be sent. Normal1400-byte DHT transport budgeting remains separate.
Source fixtures use exact original one-stream constructor/serializer bodies over
qualified original helpers/pure frozen classes, with supplied clocks/network/
IDs/provider/cache state and unchanged ReferenceFence. Nested helper namespaces
and the supplied inert transport bridge are recorded in the reference manifest.

Owned UDP integration, lookup scheduling, anti-spoof state, contact/provider
lifecycle, routing/store/persistence and host/plugin activation remain required.
This component is not an operational DHT or a complete client/plugin release.
