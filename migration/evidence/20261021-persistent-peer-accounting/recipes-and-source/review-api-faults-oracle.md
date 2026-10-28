# Independent API fault-fixture source review

**PASS for the bounded staged API-policy/older-request scope, with the accessor
isolation limit below.** This is not causal LT callback/final-counter certification.
All three complete sources, contract/README/link plan, fixed-source maps and
recorded ELF-symbol evidence were read; all declared hashes and current expected
974f engine/core/fixture bytes match. Exact inputs: `review-api-faults-oracle-pins.json`
SHA `c1d4bf06b100b3c7e3e7aa95fa7fd4a1d3a522f4a5c6231170b5adfc517b0050`. Capsule manifest SHA
`c1cc6b082c43fa3b266d6e907f4940b056bebce3e8cf815dfa0e4aabf640ffed`.
No build, test/native/loader execution, import of author tools, profile/network
access, Git mutation or source edit occurred during this review.

## Borrowed alerts, primitive copies and response provenance

`persistent_api_faults.cpp:110–147` calls actual pop once, then inspects/filters only
that returned vector. It retains no alert/span in State. The four real named
counters are copied while the actual stats alert is valid; the separate accessor
seam deliberately calls the real accessor for this provenance log. Matching
removal stores only an actual weak handle; it cannot keep the torrent alive.

Hold mode returns an empty vector without calling real pop at all for three
turns. It neither detaches nor guesses the lifetime of a borrowed old object.
The old-response test arms an actual cadence post, records actual post/delivery
order and requires a distinct later fresh post/delivery before the close receipt.
It checks no extra post while the old request is pending and a newer receipt
sequence, rather than using equal counter values as proof. This is legitimate
older-API-request delivery evidence after actual wrapper hits are observed. It
does not prove exactly when LT constructed that response or any socket/disk
callback was pending before removal.

Stats/removal loss cases erase actual pointers in the same batch and assert the
production lost-provenance/no-settlement or poison/no-completion/no-reuse outcome.
They are fixture-filtered loss, not actual LT allocator pressure/drop evidence.
The direct post exception is before the real call and correctly tests an
attributable stats-only error independently of transfer/storage success.

## Mapping and synthetic counter policy

Metric faults mutate only the returned owned vector. Missing/duplicate/type/
negative/colliding/out-of-span variants preserve original real indexes separately
for actual primitive logging. Vector growth/erase does not leave a subsequently
used invalid iterator. The synthetic accessor copies actual values to a fixed4096
scalar buffer only after checking length and every index; the input span has the
pinned signed ptrdiff_t length. Negative/regression/combined<payload/empty cases
are explicitly synthetic. Actual alert allocation/vtable/object bytes remain
unchanged. Positive actual prior counters and real wrapper hits are required;
unsupported span size/ledger overflow fails instead of silently skipping.

**Scope limitation:** `wrappedCounters` at183–201 has no session receiver and uses
only the thread-local active Scope and mode. It would also affect another
session's accessor on that same armed thread. Thus the contract/README's broad
other-session pass-through sentence cannot be credited for this accessor (nor
for global metrics, whose exception is already documented). The nine staged
methods have exactly one armed stats consumer and create the seed before Scope;
their provenance log bypasses the accessor seam. Driver confirmed that intended
single-consumer boundary during review. Approval is restricted to it. No
receiver-isolation credit is granted from these tests. If future consumers need
it, use a separately reviewed amendment with current-batch membership cleared
before each real pop, never retain borrowed identities past that lifetime.

## Thread, gate and cleanup behavior

State and noncopyable Scope are used as caller-thread fixtures; thread-local
activation keeps other threads out. Post/pop/remove additionally bind the actual API receiver.
CallGate independently binds receiver plus actual caller thread at the first
post. Its one registration is for one live owner only; an address is not authority
across destruction/recreation. Another session must not win initial binding.

Gate operations are serialized by one mutex; condition waits release it. A visit
announces entry and waits, never invokes an arbitrary observer under that lock.
Release precedes owner join. Gate destructor releases, waits for blocked visits
to leave, then unregisters under the lock, avoiding a dangling activeGate pointer.
Controller/State assertions synchronize through the explicit join. The standalone
controller invokes no owner method. Local destruction order is controller→gate→
owner→Scope→State→lease→generated fixture; early assertions and construction
exceptions therefore release before abort/join. For app tests declare Bridge
before Gate and release before waiting for close acknowledgement; never block
the GUI in waitEntered when that GUI must drive/release the operation.

## Test scope and actual-build gates

The nine behavior methods expand to17 behavior rows/19 Qt rows in source only:
pass-through/actual receipt; delayed old response; response loss; attributable
post throw; six metric faults; lost matching removal; four synthetic accessor
faults; expired close; and real CallGate entry/controller release. Counts remain
planned until actual execution. Existing generated fixture payload identity,
statistics bytes, actual weak expiration, no completion publication on fence
loss and independent old checkpoint immutability are meaningful assertions.

Each build must verify all five target-private --wrap options/undefined
relocations and actual wrapper/real-call hits. The C++ explicit-receiver/argument/
return signatures and asm names match the pinned exported symbol records and
source ABI; this static review cannot substitute for final link/runtime hits.
No LTO/ABI/source/DSO change is silently covered. The suggested180-second CTest
bound is harness scheduling for deliberate unchanged five-second efforts; no
production deadline or underlying join bound is expanded by this review.

Current tests require conditional receipt presence for their bound source-policy
cases. A future default-unbound runtime guard needs explicit test-lane context
binding/review; do not reinterpret an unbound distro pass as qualified or weaken
these assertions merely to accommodate that future default.

Actual pending positive-byte peer/disk/tick callbacks, peer-only late callbacks,
observed/dropped real global-session alerts, real dropped-alert construction,
ambiguous post-add failure, and exact dependency/runtime qualification remain
separate unfulfilled gates. No alert-constructor fabrication, copied vtables,
validity override, Java/runtime equality, wire/durability/crash guarantee or final
seal is inferred from this capsule.

Principal source SHA-256:

| File | SHA-256 |
|---|---|
| persistent_api_faults.cpp | `737a108657b9524f365c891d63f2cff75749c4ac866a5661b1c883c143c78b11` |
| persistent_api_faults.hpp | `0838d550f19401a1775c7146b8d69f72afa18cb0dc5715e23a8154871612f57a` |
| persistent_session_fault_tests.cpp | `87118c1310dfb00ac52cea9c7a2f2ee1f4e64df76721e477cc12bd1b7f0585bf` |
