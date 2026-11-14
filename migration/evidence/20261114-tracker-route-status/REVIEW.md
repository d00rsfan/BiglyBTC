# Distinguish a skipped local tracker route from a tracker error

Native `c8362a46e95ca61c39d00ad02f5661142596e674`, tree `38465d56790878b3f42c910a76946eaeae997279`. Follow-up to the source-socket/address
[review](../20261114-tracker-addresses/REVIEW.md). User observation confirmed that
the same global IPv6 source could announce successfully to some trackers while
others reported no matching local route and explicitly no request sent. A tracker
may lack a matching address family or usable route. This does not establish an
announced-private-address leak or a failure of the source IPv6 address.

The Trackers table previously called both outcomes Error / skipped. An actual SDK
announce_skipped event now has distinct per-route state4 and displays
Skipped (no route). A real tracker/network error remains state3 and displays
Error. The response continues to explain that the skipped request was not sent.
Local source socket continues to mean the socket selected for the connection;
it is not an explicit announced-IP override. Disabled endpoints remain filtered.

Overall tracker health retains its existing aggregation: a successful IPv4 route
wins over a skipped IPv6 route, while all unavailable routes still report an
unavailable tracker. This is a presentation correction; no port, bind, DNS,
tracker URL, explicit announce-IP field or wire request was changed.

- tracker-skipped-health: PASS (0.01 s).
- tracker-skipped-ui: PASS (0.53 s).

Generated health checks distinguish a skipped IPv6 route and preserve successful
IPv4 health. Qt checks show separate success, real error and skipped rows, retain
the local-socket tooltip and reject stale inspection packets. The already
qualified eight owned HTTP/UDP request captures were not repeated for a label/
route-state change. No live profile/process, SDK, preview image or launcher was
changed. The labels enter the next user-requested preview build.
