# P06-22 Original Azureus STORE admission policy

Status: implemented bounded policy; fresh GCC16/Clang24/ASan+UBSan+LSan pass.
Evidence: migration/evidence/P06-22-azureus-store-policy/{matrix.json,review.md}.
Pin before implementation to original
cb8c6cb9bb97916cbc7e80338a017200ffc3e71f DHTControlImpl.storeRequest,
computeAndCompareDistances/2 and DHTTransportStoreReplyImpl. Characterize exact
original bodies with supplied inert contacts/router/adapter/database/seed/time/
authorization contexts. Preserve contact-alive before count mismatch, default
DT_NONE1, source forwarding classification, unseeded and verification order,
large-network K expansion,10-second sender rejection memo (including signed64
wrap), furthest-distance sign, hasKey/closest short circuit, ordered store/block
queries and first block reply including nullable request/signature getters.

Native standalone owner-thread policy stages owned intentions only. Authorization
is synchronous and called only at the original point; no precomputed warming of
an authority cache for an unseeded forward. Preserve partial event/error order.
Actual routing selection, DB mutations/expiry/persistence and UDP/plugin activation
remain deferred. Record supplied snapshot boundaries and explicit native caps/
thread/reentry/authority errors without advancing full integration baselines.

Exact source differential fixtures and fresh vectors; callback invocation/order,
state memo boundary/wrap, owned replies, limits, thread/reentry and failure tests.
Fresh GCC16/Clang24/ASan+UBSan+LSan. Lower token/owned-UDP sources stay unchanged.
Only public generated contexts and unchanged ReferenceFence, no Control ctor or
normal core/profile/DNS/network/real identity/plugin starts. Save immutable
checkpoint/provenance and continue STORE/lookup/routing/storage/host integration.
