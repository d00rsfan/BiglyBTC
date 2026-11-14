# Public chat NAT directory compatibility and normal exit

Native `b677405fd0e453890553d4c9f2ef6cdf8109aef9`, tree `4e21e0fcc2237aa7dcc588e8b3eb82570b243a20`. Original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`; azmsgsync
`4209aa5029e3035cbb09dc3a83e9491d3f36fbc6`.

Public sync carries the original optional z rendezvous contact map only from an
operational main relay. An incoming hint is retained under the original
more-messages/equal-with-deletions condition. It grants no peer or private
identity authority. Explicit relay and directory lookup keep the original
alternative paths. Physical route changes retain existing room UIDs and update
the next pending DH phase without changing signed participant identity.

The directory follows original SHA1(key20+offset) diversification: frequency
reads one random branch, size reads two distinct branches; publications visit
all ten frequency branches or two fixed size branches, retaining the root for
publication/removal. Traversal is bounded to32keys/depth4 inside the original
operation deadline. Derived rounds retain known seeds; callbacks are bound to
their actual queried key across both receipt and fresh source job.

Original dht/[netN]/diverse.dat remote key/type/exp/fpo records are read and
merged through the actual profile broker. Fixed branches survive reopening;
local records, unrelated remote records and unknown root fields are retained.
48-72hour wall expiry is converted to a bounded process-local lifetime. New
branches wait for successful durable persistence. The exclusive .saving write,
file/directory identities, complete preimages and published result are checked.
Pending or unsafe files are refused without overwriting them. Reading follows
original primary-before-saving precedence. No full profile copy is made.

Normal desktop exit requests best-effort withdrawal before hard stop, bounded
to750ms; no publication returns immediately. The hard-stop API still revokes
the witness immediately and permits no later network effects. Final readiness
publishes the current snapshot before its atomic completion flag. Emergency
exit/destruction retains the hard-stop path.

Qualification: original-format profile preservation/reopen/expiry and unsafe/
pending controls passed in0.01s; actual application selection/publication/read/
punch plus bounded pre-stop withdrawal passed in5.74s. Full directory cases,
including frequency/size branches, restored fixed subsets, and held/refused
persistence, passed in4.03s. Sync codec/binary-contact regression passed0.54s;
actual SDK public/private room routes passed1.65s. Retained private owner, NAT
session and source/Call tests are recorded in the preceding owner milestones;
unchanged long suites were not repeated.

Qualification exposed and fixed a new optional-map lifetime error: parsed
contact views must be encoded while their owning document remains alive. It
also exposed stale readiness snapshots. New shard fixtures initially used the
wrong network and omitted mandatory FIND_NODE positions; those fixture defects
were corrected without extending production waits. Derived seeds and key-bound
callback lifetime were hardened during investigation. All failures remain in
the local journal. Same frozen SDK, GCC16/C++23/Qt6.12; no sanitiser claim.

The primary Public-chat NAT dependency is implemented and qualified on owned
SDK sockets. Real PublicBound/router NAT and full combined original-Java UDP
runtime interoperability remain unmeasured; see the live acceptance task. The
previous exact wire/contact/binding/client oracles remain separate evidence.
Generic non-chat client data, secondary relay de-duplication and complete global
DHT storage behaviour are outside this bounded component. No live image,
profile or launcher was changed for this source milestone.
