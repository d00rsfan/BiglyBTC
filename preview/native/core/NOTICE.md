# BiglyBTC bounded core library

Experimental C++23 components for bounded bencode/document handling, saved
library/cache projections, torrent geometry/file policy and a bounded executor.
This package does not contain the torrent engine, profile writers, desktop or a
running plugin host. Its C++ interface is not a stable binary ABI.

Original BiglyBT behavior baseline:
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f,
https://github.com/BiglySoftware/BiglyBT.
The native source commit and exported file hashes are recorded by the public
repository's SOURCE_PROVENANCE.json. Per-component source, fixture and validation
records remain in the migration review repository; no complete feature parity or
release verification is asserted by this package.

Distributed under GPL-2.0-or-later; see COPYING. Original source notices and
behavioral provenance remain applicable. Plugin consumers retain their own
licenses; azrating's data-derived code is GPL-2.0-only.
