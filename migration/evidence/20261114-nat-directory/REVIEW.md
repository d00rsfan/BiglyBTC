# Source-bound NAT rendezvous directory

Native `6afba1c77103c32d232361080d1c53290273a287`, tree `5090091bfe69cbe13dd1e193e36c20ff26fad629`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

Directory uses the original raw node-ID/:DHTNATPuncher key followed by normal
SHA1, original version0 contact payload and existing actual DHT transport and
LookupController. Reads have a20s budget and one-value limit. Pending reads are
shared; positive/negative results cache for120s. Native bounds are eight active
works,128 cached subjects and16 waiters per subject. Returned contacts are
untrusted hints; the directory grants no DATA alias or private identity authority.
Source epoch/held-permit checks protect cache completion as well as network turns.
Callbacks follow source release and service lifetime. Closing between release
and final delivery rejects cached success; destruction suppresses delivery.
The same callback lifetime/release rules now apply to NAT session completion.

Publication is restricted to this lane's own local NAT key. Its caller supplies
an externally reserved original durable version. The actual local database
creates the value; a Node-kind lookup retains actual FIND_NODE random-ID tokens,
then sends authenticated STOREs to at most20 replied closest contacts. Withdrawal
uses the original database deletion value. Read results require the requested
originator, supported family/version and current address scope. This is a native
source-bound safety restriction on unsigned directory hints.

## Qualification

Owned actual SDK IPv4/IPv6 publication→STORE→read, positive cache, deletion and
negative cache passed in1.57s. The fixture also reads the actual seed database to
check the original hashed key, publisher and exact contact payload before network
lookup. Four remaining directory entries passed in the earlier2.50s run: pending
read de-dup/cancel/source change, close between release/delivery, destruction and
wrong-origin rejection. Their production/test bodies did not change afterwards.
NAT destruction/cancel/source and cold-ingress four entries passed in1.36s; two
retained shared UDP roundtrip entries passed in0.48s after shared fixture ownership
changed. Final focused directory build passed in24.42s. The cache clock seam
advances120s without a long green sleep; physical RPC deadlines use real time.

Initial builds caught one strict indentation warning and a fixture unique-to-
shared ownership conversion error; both were fixed. The first IPv6 graph failed
because original protocol55 IDs on ::1 use port%8: random fixture ports could
collide with each other and correctly trigger self-node filtering. The fixture
now selects distinct actual observed node IDs; production ID/lookup rules are
unchanged. Only the affected two-family graph and pending regressions repeated.
Exact original codec/contact policies are retained prerequisite evidence, not
represented as new combined Java runtime interoperability.

Application automatic selection/publication/lookup integration, asynchronous
chat/DH rerouting, diversified key following and reachability/graceful cleanup
still require integration. This directory API alone does not complete the NAT
or private-chat chapter. Router NAT/PublicBound physical acceptance remains open.
Actual version reservation will be qualified with the application broker.
The user's frozen image, SDK, profile and launcher are unchanged. No separate
reviewer, subagent, public peer payload or real-profile test was used.
