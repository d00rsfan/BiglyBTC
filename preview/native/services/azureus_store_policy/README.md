# Original Azureus STORE admission policy

Standalone C++23 owner-thread policy pinned to original
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f DHTControlImpl.storeRequest,
its XOR-distance helper, DHTTransportStoreReplyImpl and DHTFactory.isLargeNetwork.
Routing/database state is supplied explicitly; output records ordered intentions
and original reply getters. Event indices refer to the supplied request; a later
host executor must retain that request when applying intentions. This component
performs no router/database mutations and starts no networking or application.

Source order is preserved: contact-alive before key/set count mismatch; DT_NONE1;
classify forwards from value originator IDs; reject unseeded forwards before
invoking authorization; then check tokens with the direct flag. Authorization is
synchronous at that exact point, so rejected unseeded forwards do not warm its
cache. Native callback errors fail closed and preserve partial event order.
Tests link the actual token authority, while Java source fixtures supply explicit
authorization results to characterize admission independently of cryptography.

Network0 alone expands K by K/2; network4 does not. Signed32 expansion and signed64
elapsed subtraction preserve Java wrapping. A distant forwarded sender is memoized
for10 seconds; repeats still verify but don't refresh the timestamp or repeat
closest selection. Local key ownership uses hasKey/closest short circuit. Every
admitted key produces a store intention; only the first nonnull block descriptor
is retained, with subsequent store intentions continuing. Block reply fields and
nullable diversification remain distinct even when block request is null/empty.

Actual closest routing lists and per-key database responses are supplied inert
snapshots. Actual DB writes, key blocks/certificates validation, expiry/persistence,
routing promotion, UDP activation and dependent plugins remain future host work.
Fixed IDs20 match UDP contacts. Native bounds255 keys/sets,255-byte keys,8192
value origins and8192 per block field cap work/memory before any intention.
Invalid snapshots, wrong owner, reentry and callback/provider errors are explicit;
partial error results are diagnostic, never sendable. Caller keeps the policy
alive through its synchronous callback. Whole-source integration stays null.

Four CTest rows cover original admission fixtures/native bounds plus unchanged
authority fixtures/bounds. New Java helpers run exact source bodies and actual
frozen HashWrapper with supplied contacts/router/DB/auth/seed/time, under unchanged
ReferenceFence. No ordinary Control constructor, profile/real identities/network/
core/plugin/storage activation is involved. Fresh compiler/sanitizer qualification
and evidence live in migration/evidence/P06-22-azureus-store-policy.
