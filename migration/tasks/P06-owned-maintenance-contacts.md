# Owned Azureus maintenance and contacts

State: in_progress; bounded opt-in slice qualified.

Source `0f7e9216253a7881fae11e1a7dae8a849cb333e8` qualifies opt-in owned Azureus timed maintenance/contact persistence and explicit Enc4/X25519 LeaseSet2 keys. Actual GCC/Clang/leak-enabled ASan+UBSan suites 16/13/13, private 16, both-JAR original objects (4 cases per reference and direction), and real isolated-router Enc4 tracker exchange pass. [Review](evidence/20261029-contacts-enc4/REVIEW.md).

OwnedLoopback uses genuine 10-second timer work and sixth-tick refresh, with profile-I/O jobs and one-shot current packet permits. The five-minute idle boundary has a pure policy control; it is not a real five-minute operational wait claim. Original raw contacts persistence uses held-profile authority, compatible contact framing, bounded selection and restart. Current adapter supports nonnegative network paths; signed-negative original pathnames remain unsupported. Cancellation, stale work, initial/reply preflights, source changes and shutdown retain finite authority. Kernel-stall and complete resource/performance parity are not claimed. Original cb8c6cb9bb97916cbc7e80338a017200ffc3e71f remains the source baseline.

The explicit AllowPeer-to-normal-routing contact attachment seam, public/default/Host routing and I2P backend activation, signed-negative network paths, multitype lease keys, remote ECIES ratchet/multihop discovery, native Streaming window/performance and the broader P05–P13 remain open.
