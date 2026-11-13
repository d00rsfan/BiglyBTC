# Original normal DDB value envelope primitive

Native `ffffc58a625b0657565298d3c065101a981f7551`, tree
`2eb17fbb7ea37935f0019cde110e93fdb2bb943a`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The standalone no-allocation normal-value parser follows the selected
DDBaseImpl.listenerMapper.valueRead normal branch. Only exact flags4 are packed;
combined4|ANON and every other flag remain bare. Packed records contain a header
and big-endian16-bit length/payload entries. Zero-length and duplicate entries
remain separate value events. Already emitted prefixes survive malformed tails.
A missing header or odd length trailer corresponds to the original thrown error
and prevents continuation; an oversized declared length corresponds to its logged
break and still permits header1 continuation. The parser reports these outcomes
safely through ranges/faults, without Java's out-of-bounds access.

Input is bounded to the existing512-byte transport value quota. Larger direct
native inputs are refused, with no original equality claim for that extra bound.
Ranges borrow immutable caller bytes; no keys, contact/version metadata, source
permissions, event dispatch, stats query, SHA1, network, profile or completion
lifetime are inferred. The actor does not use this new primitive yet. Runtime
packed/chained/diversified integration is the next milestone of the active
advanced DDB chapter, which is not declared complete here.

An exact selected original normal block is extracted and compiled with explicit
in-memory event/log/mock-GET and standard-SHA1 adapters.809 generated cases cover
all256 flag values, sizes0..512, valid/partial/odd/oversized entries, duplicates,
zero-length events and continuation intent with27/20-byte application keys.
The comparison executes with zero skips, and four Qt rows pass in0.279seconds.
Native-only quota/range checks also pass. [Scope/digests](source-scope.json)
distinguish the selected-body comparison from full original DDB/network or
completion semantics. No sanitizer, platform, stats-reader, event metadata or
production integration credit is claimed from this primitive check.

No daily-use executable, live profile, SDK, running process or preview manifest
changed. Continue the actual shared actor continuations under the chapter task.
