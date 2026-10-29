# P07-02b bounded I2CP initial prerequisite

State: in_progress; feature F06.i2cp_initial_session. Pure codec implementation ef494c64add015ea84df93ebcfbff1c7a38fa10b remains qualified separately in ../evidence/20261029-mixed-i2cp/verification.json. Initial loopback QtTCP channel implementation b998d3076cd34513ae5459adb7de03553c0b1844, qualified at test-only successor 82ef47fb32c30f57ebc9e135bcbffb00b4f7660f, is recorded in ../evidence/20261029-active-trackers-i2cp-channel/verification.json. Source integration fields remain null.

Original i2p-java9134f808337b401e8e53c73734c81fab04280c9d and plugin context33d97eca67a9f332e3d4da7e7492ec54f4bec078 remain separate. Seven78-byte original message vectors retain actual official/modified artifact origins from the earlier twelve-case-per-reference offline comparison; no Java replay or whole modified-core equivalence is claimed here.

Support bounded5-byte framing/payload128KiB and typed no-auth initial messages. The new caller-owned channel sends0x2a/GetDate only to explicit numeric IPv4/IPv6 loopback, observes one nonzero SetDate and closes on unsupported/malformed input or disconnect. Absolute default30s deadline (caller1..30000ms), generation cancellation and queued reentrant notification guards apply. It does not change Clock or create/accept any session. Current actual three-lane channel tests and one private whole-channel command are distinct from old pure codec/offline evidence.

Authentication/properties, non-ASCII conversion, original malformed/trailer permissiveness, remote/TLS, CreateSession/config/signatures/lease sets/data/lookup, profile/router startup, application activation and full session lifecycle remain unsupported. All corresponding P07 completion/performance remains open.
