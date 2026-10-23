# Original Azureus reply position framing

C++23 bounded wire framing and Vivaldi v1 float fields at original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This continues the PING reply path for
the original DHT used by ratings, related-content and message-sync. No network,
DNS, profile, ordinary core/plugin manager or JVM runtime is started.

`vivaldiV1` creates owned 16-byte position data from IEEE float32 bits, preserving
negative zero/infinities and canonicalizing NaNs as Java DataOutputStream does.
`encodePositions` borrows trusted provider results for the call, emits generic
or legacy framing and returns owned bytes. It preserves null/partial serializer
failures, count byte narrowing and the pre51 fallback NaN position. Other
providers may supply opaque outgoing bytes; no coordinate algorithm or incoming
provider implementation is inferred from those bytes.

`decodePositions` receives explicit Vivaldi v1 registration context. It owns
recognized position data and trailing bytes. The known provider reads 16 bytes
regardless of declared size. A failed known read resets before original signed
size skipping; unknown negative sizes consume no payload. Legacy failure can
return a null position with untouched input. Provider failures are diagnostics,
not proof of valid coordinates. Future coordinate validation/update policy,
registration/listeners and lifecycle remain host work.

Inputs/outputs and outgoing provider scans are bounded at 8192 bytes/entries.
Failed partial outputs are diagnostics and must never be transmitted. The future
transport must also enforce full header/body packet limits and action semantics.
This codec intentionally preserves bounded malformed framing behavior; it does
not certify a packet as a valid transaction or authenticated peer.

CTest compares 8,554 original observations, plus limits, provider reset/registration,
signed skips, partial writes, copy/input ownership, NaNs and count wrapping.
Qualification adds 2,494 source encoder comparisons and 4,988 accepted/failure Java
readbacks per compiler, including 1,024 fresh IEEE vectors. The reference copies
exact original framing/manager-deserializer bodies with supplied providers and
no listeners, and uses unchanged frozen VivaldiPositionProvider/Factory/Impl
classes. It never initializes the normal Java position manager or runs coordinate
estimation/update/randomization. Exact source/class/fixture/binary hashes and
scope are recorded in P06-10 evidence.

Reply alternative contacts, full PING header/body assembly, owned-loopback UDP
transactions, routing/storage/persistence and plugin/runtime activation remain
required. This is a shared codec, not an operational DHT or complete plugin.
