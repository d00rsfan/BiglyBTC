# Explicit Enc4 LeaseSet2 keys

State: in_progress; bounded original-format and local-router slice qualified.

Source `0f7e9216253a7881fae11e1a7dae8a849cb333e8` qualifies opt-in owned Azureus timed maintenance/contact persistence and explicit Enc4/X25519 LeaseSet2 keys. Actual GCC/Clang/leak-enabled ASan+UBSan suites 16/13/13, private 16, both-JAR original objects (4 cases per reference and direction), and real isolated-router Enc4 tracker exchange pass. [Review](evidence/20261029-contacts-enc4/REVIEW.md).

Generic absent encryption policy stays Enc0. Explicit `4`/`ECIES_X25519` selects a genuine independent 32-byte pair; it does not alter Destination, signing identity, keyfile or hash. Renewal reuses the owned pair and acknowledged merged configuration seals its type. Unsupported multiple/other types refuse without alternate-key fallback. OpenSSL supplies the existing raw X25519 primitive and secret erasure; no native ratchet implementation or new dependency is claimed.

Actual official and modified Java objects verify four DSA0/Ed7 one/sixteen-lease cases per reference and direction: eight Java-to-native frames, four native-to-Java frames and changed-signature rejection. The exact 14 official source objects are retained; historical lazy-missing metadata is preserved. A real fenced router accepts current signed one-key Enc4 LS2 for both unchanged Destinations and carries signed2 connect/claimed3 scrape/RAW18 counts3/4/5. Warm signed-cache/zero-hop is an explicit fixture precondition. This qualifies neither anonymous remote tunnels nor default plugin/Host activation. Official9134f808337b401e8e53c73734c81fab04280c9d and modified b0866aa843a55d9290755c7a5cc3091cda765374544914afb4d3557ae99994b5 remain distinct provenances.
