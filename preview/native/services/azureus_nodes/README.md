# Original Azureus node identities and UDP contacts

C++23 wire/identity component at original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This supports the next DHT lookup
work needed by ratings, message-sync and related content. It performs no DNS,
network, ordinary manager/core/plugin startup or profile access. Its pure library
depends on OpenSSL3.5.5 and standard threads; Qt is only in integration tests.

`identityKey` preserves signed-byte protocol thresholds32/33/50, original IPv4
restriction arithmetic, port modulus, Java IPv6 eight-group spelling and numeric
scope (including explicit%0 versus unset scope). `unresolvedIdentityKey` accepts
only the exact dht6.biglybt.com seed hack, without resolving it. A resolved address
type is supplied explicitly; forced mapped IPv6 and an already normalized IPv4
address therefore remain distinct as in Java. Interface-name discovery/scope
names and persisted contact map/import behavior are separate host work.

`NodeCache` is a bounded128-entry access-order owner, returning owned20-byte IDs.
It is neither copied nor moved, so internal list iterators cannot alias another
cache. Cross-thread calls fail before touching state. Keys/IDs match the source
history under read-only use; Java's accidental shared-array mutation is excluded
from parity. Copies of returned values cannot poison the native cache. Hashing
uses the documented OpenSSL [EVP digest API](https://docs.openssl.org/3.5/man3/EVP_DigestInit/),
with a maximum-sized scratch buffer and an exact20-byte result check. Crypto
failure produces no synthetic ID or cache entry.

`encodeContacts` borrows outgoing snapshots and emits original CT_UDP1, protocol
and address fields, with an unsigned16-bit array count. `decodeContacts` owns
addresses/IDs/tails, preserves source truncation/width/port consumption and mapped
normalization, and computes IDs before filtering port0 records. Instance/skew/
generic flags are reset to zero on this wire path. A later malformed record
retains prior node-history effects. No contact/provider/routing registration or
untrusted endpoint activation is performed.

The codec cap is8192 bytes. Large declared counts cannot cause preallocation for
unreceived contacts: capacity is limited by actual packet bytes/minimum record
size. Failed partial outputs are diagnostics and must never be sent. Normal
DHT1400-byte datagram budgeting still belongs to the transport.

CTest compares87230 source observations:84153 identity/cache queries (including
every UDP port),338 serializers,2736 parsers and3 cache resets. Native tests add
ownership, thread/crypto failure, cache eviction, scope/seed behavior, mapped
offsets, filtered/partial cache effects and exact/oversized packet limits. Java
readback adds849 encoder/contact comparisons,1024 fresh identity vectors and6
coupled cache-prefix observations. The guarded reference copies exact original
bodies and uses unchanged frozen SHA1Simple, with supplied transport/getters and
empty provider state. The recorded runtime is UTF-8 Java17.0.20; all qualified
identity keys are ASCII. Interface scope names/default-charset differences are
not hidden as full platform parity.

FIND_NODE/lookup responses, contact lifecycle/positions, anti-spoof state,
routing/storage/persistence and plugin/host activation remain required. This
component is not an operational DHT or a complete native plugin/client release.

`encodeContact`/`decodeContact` also expose the same single-record path without
array framing or port0 filtering. This supports transport-value originators,
which the original source retains even at port0. Array parsing/writing shares the
same record routines and keeps its existing filter. The single decoder returns
owned contact fields/ID and consumed count, avoiding copies of the remaining
packet for each value originator. It does not perform contact activation.
