# Private SDK bootstrap before payload preparation

Original core `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`;
I2P helper `33d97eca67a9f332e3d4da7e7492ec54f4bec078`.
Native implementation `2c6443e7ffd1732b75176943066d1749383ef0f9`,root tree `cec62bf1fb5e7ddbe0550a3ae85d0196b76c7feb`.
Prerequisite exact SDK roles `eecd9ace1417e6ac4f5c0c17bbd4e9ca8a81177d`.

Private ordinary/pending bootstrap now constructs the actual isolated SDK
session after source/key capture and before payload/member preparation.
The anchor's session settings equal the former child settings:the settings()
projection is independent of per-torrent identity/tracker selection and disables
all direct/public discovery/listeners/proxies/SAM defaults. No bridge/provider
registration or payload resume occurs during construction.

OwnedI2pService releases its initial configuration/source gate before calling
the owner preparation. The owner rejects a still-held outer gate before SDK
creation. It checks the optional captured configuration before creation and
again under the source gate immediately before storage/pending preparation.
The service reacquires and rechecks the exact queue/rate configuration before
opening contact state and before starting the private Qt event loop. This
preserves the actual source/config precondition across the newly unlocked SDK
wait. The optional configuration string only adds a refusal condition;it does
not grant profile/storage or tag-rate authority and is not retained by the owner.

The regression wraps the actual linked session(session_params&&) constructor.
It invokes the genuine SDK constructor,observes the generated payload directory
at entry,and asks another thread to acquire the SAME retained profile's source
gate. The prior implementation fails both ordinary/direct and service controls:
storage already exists at constructor entry;the service also blocks the reader
behind its outer gate. The recorded expected failures retain the actual outputs
and exit1 rather than being counted as positive implementation checks.

Nine final controls cover direct/service ordinary and persisted I2P magnet
bootstrap,caller-held gate,stale captured configuration,an injected constructor
failure,configuration mutation during construction,and the existing private
active-tag-rate refusal. Invalid source/config fails before payload preparation.
Constructor failure leaves payload storage untouched. The active-rate control
constructs the isolated SDK but still refuses before ManagedStorage mutation;
this stage deliberately does not attach private rate classes or lift that fence.
All fixtures use disposable generated profiles and known synthetic key inputs.

GCC16 executes the actual Native targets. Scoped ASan/UBSan and Clang24 compile
the three changed/test translation units,linking the recorded exact Release
SDK/Native archives. Qt,SDK and other archives retain Release scope;no full SDK/
application sanitizer or TSan claim follows. The unchanged production source
and archive digest maps allow the final ninth fixture case to relink without
recompiling the already qualified production variant objects. Final fixture
inputs and logs distinguish the earlier eight-mode fixture from the final nine.

Existing I2P Host/tag Host/profile/source and actual old-image deployment
controls remain required. The ordinary ABI1 application/optional-API/profile
checks remain independent of the isolated all-role ABI2 cache. SDK source,
artifact,role manifests,selected working SDK,delivered daily,launchers,running
client and real profile/payloads are unchanged.

Next:actual common rate-origin/private SDK classes and sealed source/cohort
pre-storage preparation,paused exact-handle attachment and complete Host
post-commit effects. SourcePermit retains its separate worker's gate through
Ready/Executing;do not introduce a callback which reenters filesystem preparation
while that worker waits for acknowledgement. Common global/LAN ownership and
whole F13/P08 remain in progress. Component integration baselines do not advance.
Continue the feature without stopping at this bounded prerequisite.

Review by the implementing agent;no separate reviewer/agent was used.

Final results:38 positive runtime groups;nine modes per GCC/scoped SAN/Clang and 127 Native/ordinary Qt rows with no failures/skips. Existing SDK deployment and ordinary ABI1 controls pass. The final Native bootstrap build and ordinary application build pass;before-fix defects remain explicitly expected exit1 controls.
