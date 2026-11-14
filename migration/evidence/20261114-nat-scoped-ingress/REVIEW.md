# Source-bound first-contact NAT in the actual chat owner

Native `a1977de9a7e265c8aedfc21561870890fa59628a`, tree `d312e1dc79bbaf87b9cc1b90d2bf455281c745c6`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

The existing Public chat UDP owner now registers one NAT session on each main
lane. Exact NAT DATA is dispatched before ordinary unknown-peer admission.
First-contact single-fragment control requests are bounded to16 queued items,
decoded after source release, and granted only a finite NAT-handler DATA lease
under a fresh lane permit. Bind/Punch/Query/Close can establish control contact;
Connect must match the selected relay, and raw6/7 must match a live expected
source IP/window. Full network/header/body, same-IP external/physical and mode/
key constraints remain enforced. Other handlers and ordinary routing are unchanged.

The session's trusted peer hook also grants finite routes before its control
calls/writes, preserving the actual port separately from external identity.
Shutdown and changed network settings revoke NAT before the transport closes.
Without that hook, the previous immediate refusal for an unadmitted endpoint is
preserved. No automatic relay discovery, publication, chat rerouting or private
fallback is enabled here; those remain the next integration steps.

## Validation

The actual GeneralChats service, its generated profile, source IO broker and SDK
listener successfully served a three-party bind/connect/punch exchange in7.89s.
The caller initially guessed the wrong physical port; the early inbound packet
established the correct route while preserving original external/node identity.
This is an application-owner runtime test, rather than only a service-model test.
Two-family cold first-contact tests cover the same sequence on owned SDK sockets.

The initial full actor run passed ten of twelve entries; two prior explicit-
lease tests exposed loss of immediate refusal when no automatic hook exists.
That production precheck was restored and the two failed entries passed in0.78s.
After reusable helper extraction, both cold-ingress and both explicit-lease
entries passed in1.41s. Existing shared SDK roundtrip/address-role cases passed
in1.34s; the two roundtrip entries repeated after fixture extraction passed in0.52s.
Three affected real-owner private/history/busy-shutdown cases passed in66.51s;
their production paths are unchanged by the subsequent fixture-only extraction.
The new owner test's first5s readiness budget was too short (observed5.15s), so
only that fixture allowance was raised to10s. The final focused build passed
in47.82s. Failed preparation/setup runs receive no qualification credit.

The shared fixture retains its actual SDK witness and supports an explicit
owned test-network parameter. Real private source-hook assertions check held
revision reads, after-release decode and non-nested permit acquisition. Generated
declared-port changes remain a header seam, not actual external NAT mapping.
No real profile, public peer payload, runtime image, launcher or SDK changed.
Physical PublicBound/router NAT acceptance and combined original-Java NAT
interoperability remain open. Earlier exact original wire/contact/policy evidence
is retained separately. Whole NAT/private-conversation chapter remains open.
