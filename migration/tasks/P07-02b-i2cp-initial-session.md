# P07-02b bounded I2CP initial message prerequisite

State: in_progress; feature F06.i2cp_initial_session. Implementation ef494c64add015ea84df93ebcfbff1c7a38fa10b. Exact acceptance is recorded in ../evidence/20261029-mixed-i2cp/verification.json; source integration hashes remain null. No connected session/router/client transport or public-network activation.

Original source is i2p-java9134f808337b401e8e53c73734c81fab04280c9d. Original plugin33d97eca67a9f332e3d4da7e7492ec54f4bec078 supplies context, not a replacement claim for modified azi2pcore source. Exact official and modified artifacts are observed separately; whole modified-core source equivalence stays unknown.

Support5-byte framing with128KiB payload bound and one owned frame per consume; partial EOF, invalid length or allocation failure requires explicit stream reset. Typed no-auth GetDate32/SetDate33/Disconnect30/SessionStatus20/Destroy3 preserve ASCII, nonnegative raw dates and documented status0..4. Date zero is null sentinel, not clock authority. Original malformed/trailing-reader permissiveness, authentication/properties, non-ASCII conversion, negative dates, internal status5, create/config/signatures/data/lookup/session state machine and actual transport remain separate unsupported work.

The published receipt distinguishes fresh pure unit execution on each lane from one actual offline GCC replay against twelve concrete original writer/parser cases per exact Java reference, plus authentication refusal. No prior identity/datagram PASS is reused as new I2CP acceptance. The source-only allocation invariant finding and corrected poison-on-exception behavior remain in evidence; no allocation-fault runtime injection is claimed.
