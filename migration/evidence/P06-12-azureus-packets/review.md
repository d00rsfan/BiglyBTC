# Complete original Azureus PING packet review

Baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f. Exact original full PING
constructors/serializers run through one input/output stream over qualified
original header/helper classes and unchanged frozen pure Vivaldi/BEncoder
classes. The reply setter's contact-array allocation type receives namespace-only
adaptation; original/adapted method hashes are separate. Base wrappers supply
getters/header offsets and explicit network/clock/IDs/provider/cached-stat context.
The known-action wrapper dispatches after the PRUDP request16/reply8 prefix.
There is no ordinary transport/core/plugin/config initialization, statistics
sampling, network/DNS or user profile access. No normal DHT startup is claimed.

4252 original one-stream observations cover requests/replies across old/signed/
modern protocol versions, registered networks, IPv4/IPv6/mapped/unresolved
origins, integer/clock edges, optional/supplied statistics, position provider
registration/reset and signed sizes, first16 contact selection, empty/null/
oversized/short option arrays, partial serializers, malformed body fields,
minimum-version/network/action ordering and representative full-packet
truncations. All native observations match the recorded bounded domain.

Native tests check owning full packet/header/body copies and moves, cleared
moved-from offsets, mapped normalization with original payload offsets, header
skew on later required-body failure, optional-stat truncation, dispatch/prefix EOF,
foreign networks, first16 selection/order, provider reset before following contact
count, fallback positions, partial output and complete8192-byte packet caps.
Failed partial output is diagnostic only. encodeUpload adds a shared wrapper over
the same supplied-snapshot emitter; the original request implementation is
unchanged and its entire fixture/readback suite is replayed. Copy assignment
commits a complete owner before changing visible state.

Fresh serialized GCC16, Clang24 and ASan+UBSan+LSan builds pass all ten CTest rows
and all five source-built Java readback suites. Per compiler this replays38593
original observations (4252 packet plus34341 primitive observations),863 complete
packet encoder comparisons/1726 accepted Java packet readbacks, and all four
primitive readback suites. Total Java packet/header/position/contact/option/upload
readbacks are18605 per compiler, including accepted/failure primitive observations.
256 fresh complete packet vectors supplement the frozen full corpus. Exact75
native/tool/fixture inputs, original/helper/core classes, commands, logs and
binary hashes are recorded. Leak detection remains enabled; LSAN_OPTIONS is
cleared and no suppressions are installed. Initial local compilation required an
explicit originator default in the reply aggregate under GCC's missing-field
warning; fixed before the fresh matrix. No native fixture mismatch or sanitizer
failure occurred. No independent reviewer was used.

PING action dispatch is deliberately bounded to this selected codec; other DHT
actions are not implemented. Connection-bit multiplexing, matching endpoint/
network/transaction/connection, deadlines/cancellation/pending queues and normal
DHT1400-byte datagram budgeting are the next actor task. Lazy contact property
projection/endpoint policy, coordinate algorithms/provider lifecycle, statistics
sampling/ranking/cache/throttle, routing/storage/persistence and plugin/runtime
activation remain pending. A PING packet codec is not an operational DHT or
plugin release. No full integration baseline or release gate is advanced;
immutable implementation provenance is recorded in a later commit.
