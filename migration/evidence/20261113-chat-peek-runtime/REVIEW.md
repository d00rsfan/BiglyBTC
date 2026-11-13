# Transient encrypted chat reads and original room responder

Native d06deba1fee7856a9e4a8053c6ab2c246f7020ed, tree
0e145887016a4fe7aa21a7e15f78d859b9277182. Original azmsgsync baseline remains
4209aa5029e3035cbb09dc3a83e9491d3f36fbc6. Foundation encryption/format evidence
is in ../20261113-chat-peek/REVIEW.md. This scope does not yet expose a download
UI, original owner discovery or read-side auto-notify; those remain active work.

PeekSession has no Room, signer, UID, history or profile creation. Its owner
feeds admitted actual discovery-originator contacts. A separate outgoing-only
original DDB read uses an explicit fresh revision without registering a local
incoming handler. Existing transport physical-send/receipt/revision/address and
resource checks remain in effect. A discovered registration dependency was
removed only for this explicit transient read overload; old callers retain
their registered-handler contract.

There are at most32 distinct contacts, five active/queued sends across both
families,10seconds per Public read,60seconds caller maximum (the Public owner
will choose30),10seconds grace from the first valid reply and three accepted
responses. Discovery completion with no pending/active reads finishes early.
Close/expiry retires tickets; delayed crypto or reply callbacks cannot add
results. These are finite native policies; contacts/counts are observations,
not authenticated participant identities, new messages or delivery receipts.

Normal joined rooms register the original separate peek read handler. Reply m
comes from the actual raw message ledger, with the last raw signature/key pair;
t/l use actual tracked peers/responders. There is no synthetic local peer. The
original1000-bit address-keyed rolling Bloom estimate is ported, including100
entries, strict/non-strict120second boundaries and600second reset. Actual
selected original nodeIsAlive/checkLiveNodeBloom bodies and the real original
BloomFilterFactory agree with1184 native observations. IPv6 address keys use
Java's uncompressed host-address spelling with original port suffix.

Three configured CTests pass with zero skips in5.05seconds: original-chat-peek-
payloads, chat-full-original-java-handler, chat-shared-sdk-udp. Tests exchange
real encrypted read packets over the owned libtorrent listener on IPv4/IPv6,
check unchanged remote message/participant counts, refuse unadmitted contacts,
cancel before crypto, deny a source permit, expire an uncompleted discovery,
accept three independent real responders and ignore actual replies whose
crypto callback resumes after expiry. Existing shared calls, room catch-up,
routing and attachment cases also pass. No user profile, running preview,
SDK artifact, external public room or live Internet network was used.
