# BiglyBTC-plugin-azrating

Bounded native data model for the original `azrating` plugin. This is a static
host library, not an installed operational plugin module. It opens no sockets and
publishes nothing. The distributed database, updater/eligibility, original torrent
attributes, optional chat/reseed integration and Qt views remain required.

Original baseline: `1629340340d0bc391d9e582f9ee989a93d550581`.
The unchanged RatingData, RatingResults and KeyGenUtils reference classes run in
a fenced helper with the frozen original BiglyBT bencode formatter. The native
code preserves UTF-16 data, Java UTF-8 replacement, null/partial object states,
upper-only score cap, int32 narrowing, `.rating` key bytes, float accumulation,
comment formatting and optional result retention. Decode owns an immutable
document and does not rewrite unknown data. Complete decoding must be checked
before host use; a positive score alone is not a publication authorization.

Bounded parser:1MiB,10000 nodes,32 levels, strict canonical bencode. Bounded
aggregate:100000 positive ratings,1Mi UTF-16 comment units and4Mi retained
text units. Native limit and
parse failures preserve input; this does not claim accepting every malformed or
arbitrarily large input Java might accept. Null-comment aggregation errors retain
the original partial average/retention changes and report the failure explicitly.
Original encoding failure for int32 MIN also returns an explicit error.

Data-derived code: GPL-2.0-only, original Copyright (C) 2004 Aelitis SARL.
Do not infer an operational DDB or complete plugin parity from this data port.

The local-rating host policy reads supplied rating/comment attributes and the
current plugin nickname, then previews ordered rating/comment setter intentions
and an urgent publication-queue intention. Attribute suffixes remain rating and
comment under runtime ID azrating; an actual host must bind them to the original
Plugin.azrating.* attributes. A nickname-only change queues publication without
attribute writes. Original null-nickname errors can occur after setter intentions;
the plan retains those intentions and reports the error. No plan is applied or
queued here. Actual persisted attribute reads/writes, complete host eligibility,
durability, DDB publication and Qt editing remain required.

Integer.parseInt behavior is pinned to the qualified Java17 reference, including
all 65536 UTF-16 units, signed32 overflow and37 BMP decimal digit ranges. Both
ASCII signs are accepted without trimming; supplementary digit surrogate pairs
are rejected by the original char-based parser. Invalid score text retains zero
and a diagnostic flag; missing comments become empty. Input nickname/comment/
rating strings and replacement text are each bounded to1Mi UTF-16 units before
staging any intentions. This is a native resource limit, not a claim to accept
every arbitrarily long Java string. The model requires a non-null replacement
RatingData; host getter/setter exceptions and durable execution remain outside it.

Build with an independently installed BiglyBTCCore 0.1.0 package from the
BiglyBTC repository. SOURCE_PROVENANCE.json in the public export pins its exact
core source commit. Build/install that checkout's core directory first, then:

```sh
cmake -S . -B /tmp/biglybtc-rating-build -G Ninja \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-16 \
  -DCMAKE_PREFIX_PATH=/tmp/biglybtc-core-install
nice -n 5 cmake --build /tmp/biglybtc-rating-build -j2
ctest --test-dir /tmp/biglybtc-rating-build --output-on-failure
```

Use matching qualified compiler, standard library and sanitizer/build options for
the core and consumer. CMake performs no downloads. Fixtures replay 1819 original
observations plus bounds tests; no Java installation, user profile or network is
needed for that independent replay. Local policy adds70000 observations plus
limits/ownership checks. The inactive ci/ recipe additionally pins
the core repository commit; no hosted CI result or binary release is implied.
