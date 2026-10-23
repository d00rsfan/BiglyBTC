# Original Azureus STORE packets

C++23 full request/reply codec pinned to original core
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. This continues the original DHT
needed by ratings/message-sync. It has no network listener, profile access,
ordinary managers, storage authorization/DB writes or routing/plugin activation.

Requests preserve action1026, random ID at version7, independent key/value-set
arrays and source receive clock skew. Keys use byte counts/lengths up to255;
value sets have a byte outer count and unsigned16-bit inner value counts. The
source constant MAX_VALUES_PER_KEY255 is unused by these wire helpers:300 empty
values can fit the codec and remain accepted. Key/set count equality and sender
partitioning are separate host policies. Targets are not forced to20 bytes.
Original padded single reads, null/partial prefix order, wrapping64 skew,
unfiltered value originators and cache side effects remain intact.

Replies preserve action1027, diversification at version6 (absent/null earlier,
empty distinct), and network4/version55 optional cached uploads when bytes remain.
Missing interim uploads and truncated padded diversification match original
behavior. Header/network/minimum/action/skew and global failure cursors compose
qualified original primitives. Supplied random IDs are wire data, never active
storage authority. Full packet limits are8192 bytes; transport1400 budgeting is
separate. Failed partial outputs are diagnostics and must never be sent.

The packet owns raw bytes, keys, nested values/IDs and its computed trailing view.
Moves reset old packet offsets/body. Nested value sets use decodeValuesPrefix,
which owns values/IDs and reports consumption without copying unused remainders.
Existing value decoder APIs still return their owned trailing bytes. Endpoints
remain inert, regardless of the contact family/address/port in the payload.

The guarded one-stream Java reference copies exact STORE constructor/serializer,
key/value-set array and request getClockSkew bodies over qualified original
helpers/frozen classes. Only nested type/exception namespaces and inert supplied
packet/getter bridges adapt. ReferenceFence remains unchanged; no ordinary core,
config/plugin/profile, DNS or network starts. Fixtures contain5947 observations.
Native tests cover ownership, clocks, nulls,300 values, prefix parsing, mapped/
unfiltered contacts, exact8192 limits and crypto/thread failure. Fresh Java
readbacks cover successful packets and matching original legacy failures.

FIND_VALUE/continuations, actual token authority, lookup/routing/storage,
persistence and host/plugin activation remain required. This is not an operational
DHT, complete plugin/client release or whole-client performance claim.
