# Original private Public conversations — qualified channel/owner/menu milestones

This qualifies the private transport, actor admission and participant/policy menu
milestones. The whole private conversation chapter remains open for commands,
quit/delivery linger and remaining nickname/window lifetime parity. No current
user profile, real chat, frozen user image, manifest, launcher or SDK was changed.

## Immutable source scope

Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`: BuddyPluginBeta private construction,
chatRequested, participant selection, config policy and parent nickname/identity
inheritance; CryptoSTSEngineImpl/CryptoECCUtils and vendored BouncyCastle.
Original azmsgsync `4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`: MsgSyncHandler private constructor,
getSharedSecret/handleDHRequest, general/private AES envelopes and fixSecret;
MsgSyncPlugin private handler selection. Selected method-body hashes and test
collaborator limits are in [source-scope.json](source-scope.json).

Qualified source milestones (no whole-upstream integrated-through advancement):

| Component | Repository | Commit | Tree |
| --- | --- | --- | --- |
| DH wire, move-only authenticated channel | BiglyBTC | `becfebd7d55dce7fe4a2b8dd308277d804ecbed2` | `e689e484d3774121a00c20385671c664e727f967` |
| Private room/session isolation | BiglyBTC | `0ce50e97b2607ed2e4b6cc53a9a62ab351a877f5` | `241b440913d7be313f0e146eca551c2ce0aeb26c` |
| Imported immutable identity inheritance | BiglyBTC-plugin-azmsgsync | `2bc28c26c669734b2343215fbb2291c54e96e972` | `a183e966192f36078de45c5d93d0ac387bb9ea49` |
| Original policy, actual actor and typed menus | BiglyBTC | `e2584e777e361223d015d198f3203ccb7585c33d` | `54a9e2a19e20d8196f1c4e7541f2c45cfef996ae` |

## Resulting behaviour

The original two DH requests use version5, type2, activity8, child user-key16 and
STS keys/auth; replies use type3. Negotiation stays inside parent general
AES encryption. Private data uses only the STS-authenticated AES-192 channel,
with the original secret conversion and IV/padding. Sender key verification is
bound to the full selected signed parent participant key. No private payload
falls back to public encryption or a public-room registration.

The typed private Room has its own random UID, ledger/replay state and handler.
It inherits the already imported parent signing identity through owned immutable
EVP references, with fresh STS ephemeral keys and no private-key export/profile
reread. It accepts only the local/negotiated peer's signatures, registers only
the selected endpoint's IP-family lane, creates no peek handler, ignores routing
hints and refuses history import/export. A completed child can continue after
the parent public room closes; its source proof still binds the original saved
parent identity and the existing Public network authority.

The actual GeneralChats actor correlates activity, user key, endpoint, parent
epoch and deadlines; consumes failed exchanges; bounds/cleans pending work and
cancels transfers on closure. Expensive crypto/envelope/transcript preparation
runs after source-permit release. Every wire effect/admission takes fresh held
source authority. On incoming completion, the actor additionally reloads the
original private policy and participant pin from the real original configuration,
even when cached presentation preferences have not changed. Signed-roster
membership is checked before installing a child. Private rows cannot be joined
as public rooms, made favourite or saved as public history/read markers.

Original `azbuddy.chat.private_chat_state` is imported with default3;1 disables,
2 requires a pinned sender, and unknown saved integers follow Java's enabled
branch. Typed UI writes accept only1..3 and use primary-source expected-value
CAS, preserving unrelated fields. Global settings completion does not replace
participant preferences. The participant action captures full key, generation
and actual parent epoch; old menus cannot target a rejoined room. Private rows
are projected through the existing notification/click route; saved public-room
options are disabled for them. No foreground window is raised for a remote
request; a private row is added for incoming conversations.

## Qualification

- Both directions of Native/Java handshakes and encrypted private payloads use
  exact selected azmsgsync method bodies and unchanged original core STS/crypto.
  Contact/IPC/listener/child objects are generated stdio collaborators, without
  sockets, real profile access, UI or actual plugin lifecycle activation.
- 80 selected policy states match the actual BuddyPluginBeta.chatRequested body,
  including disabled, pinned-only, absent sender, statistics and unavailable UI.
  Native phase/replay, mismatched target, trailing/duplicate/ambiguous frames,
  moved/expired channel and inherited-key lifetime cases also pass.
- Generated actual shared-SDK negotiation/private payloads and public catch-up
  pass for IPv4 and IPv6:6 QTest entries,1.245s. Public ledgers stay unchanged,
  extra endpoints and third-party signatures are refused, private peek/history
  is absent, and ordinary public catch-up remains working.
- Grouped original/private, asynchronous profile and identity-bound suites pass
  in3.73s. Expected-value policy writes preserve unrelated configuration bytes.
- Actual GeneralChats owners with generated profiles and owned IPv4 seed/peers
  pass in25.06s: fresh external config disable, pinned-only refusal/acceptance,
  stale parent epoch, typed child exchange, no child profile/catalogue identity,
  public-join/save-option refusal and independence after public parent closure.
- Affected service/desktop-host/UI targets compile. The final2x widget suite has
 22 passing QTest entries in2.23s. The previous first-action test now selects
  Ignore explicitly, since Private chat precedes it. Synthetic screenshots were
  inspected: [page](private-2x.png), [participant menu](private-menu-2x.png).

The final UI-only source adjustment was recompiled and rerun without repeating
unchanged green network suites. No full user executable was linked or delivered.

## Limits and next milestone in this chapter

Private `/msg`/`/query` remain local unsupported commands until their complete
private route is qualified. Original signed quit status/delivery linger and
private nickname controls remain pending; current private closure is immediate
and the nickname is inherited at creation. Public favourites/history/indicator
options remain deliberately inapplicable to private rows, as in Java.

Pending-connection caps are finite per actor (17 incoming,16 outgoing,64 rate
buckets), so this is bounded operational coverage, not an exhaustive plugin
rate/lifecycle parity claim. Duplicate first requests are replayed only for the
same endpoint/user/activity/keys; changed correlations and trailing STS fields
are refused. Ambiguous/invalid frames never gain an unencrypted fallback.
Full original SWT/plugin lifecycle, sanitizer/platform matrix and real-profile
private conversations have not been qualified. I2P and lower providers retain
their existing priorities; this chapter concerns Public private conversations.
