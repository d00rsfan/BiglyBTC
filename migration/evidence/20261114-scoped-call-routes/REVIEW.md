# Scoped physical Call rerouting and observed node status

Native `a6eee96f652f1706cb185558702240f09b677e5d`, tree `697aecbd63d0093f9773a9637a3284255e3c631c`; original BiglyBT
`cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

A current registered receiver can move its pending Calls to a same-IP physical
port only with a matching finite DATA lease, source revision and unchanged
external identity/version. Request key, connection/ticket, payload and original
deadline remain fixed; partial response/write progress resets for that physical
route. Old incoming receipts are discarded rather than redirected. Other
handlers, DHT peer admission and Read/Write operations do not gain this authority.
This supplies the transport primitive needed by original asynchronous NAT/DH;
it does not itself change the full signed private peer identity or add fallback.

Existing matched FIND_NODE replies now retain observed node status and actual
packet version/instance/flags in the passive contact snapshot. Hints never invent
routability. A trusted current owner can report UNKNOWN/-1, non-routable/0 or
routable/1; defaults remain UNKNOWN. The original15-minute reachability estimator,
saved setting and automatic relay selection are not implemented by this setter.

Four actual SDK entries passed in1.76s: IPv4/IPv6 pending Call migration, no-grant/
wrong-receiver/stale-source refusal, old receipt invalidation and no DHT admission;
original call deadline retained; observed true/false/unknown node status. The
declared external port of a copied owned test channel is fixed to the old
endpoint while its actual SDK sender moves. This is a generated header seam,
not a new observed SDK/public mapping or real router NAT claim. Provider::channel
and its witness policy are unchanged. Related directory publication/read/cache/
withdrawal passed on both families in1.28s; two-family first-contact NAT passed
in0.74s. The single grouped build passed in136.48s. No full unchanged owner/history
suite was repeated. Same pinned SDK, GCC16/C++23/Qt6.12.0; no sanitised claim.

Application room/DH route consumers and automatic rendezvous integration remain
next. The frozen user image/profile/launcher are unchanged. No separate reviewer,
subagent or combined original-Java runtime interoperability is claimed. Earlier
exact wire/contact/client references remain separate prerequisite evidence.
