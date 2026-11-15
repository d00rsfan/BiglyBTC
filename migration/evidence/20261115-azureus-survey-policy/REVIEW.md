# Saved original survey activation policy

Native `c8f434fb84eb6392c870189202df00b8ee08fa63`, tree `e9d131fa1dfbd89aee911b1bfd7782084ca2786d`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Actual VersionCheckClient.getFeatureFlags, FeatureAvailability and DHTDBImpl
constructors execute on generated saved configuration. There are 23 flag cases
and 552 actual constructor decisions across protocol, network and adapter
presence. Native matches IPv4-map precedence including an empty map, IPv6 fallback,
disable bit 0x20, CVS exemption, protocol threshold and absent-adapter behaviour.
Original signed-long parsing is preserved for signs, zeroes, limits/overflow,
whitespace, Arabic/full-width digits, supplementary digits, BOM, NUL and invalid
UTF-8. The source minimum is protocol 26. Malformed field types and ambiguous
keys refuse the native helper; duplicate keys are already rejected by the core
decoder, a fact corrected in the negative fixture expectation.

The reference never calls VCC.initialise or performs a version-check request.
Native is a pure saved-state reader; it performs no file/network operation or
configuration write. The original enables surveys once at DB construction, so
the future normal owner should capture this startup decision on the existing
profile worker. Existing per-effect source/private/key gates remain required.
This helper does not disable ordinary DHT when only replication is disabled.

Focused GCC16/C++23/Qt6.12.0 build and all comparisons passed. No application
link was repeated for this currently inactive preparation helper, no new daily
or live-profile acceptance was performed. Activation in the actual owner and
source-gated neighbourhood/query/repair execution remain in progress; F09 is open.
