# Original client bind and tunnel timing policy

Native `0a510589e5eec530d3694b9745e964be292a749f`, tree `ecc4d462a32dcfbac67ac2efebaad3a78995686b`. Original BiglyBT `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`.

ClientBinding models two initial attempts, four transport failures after an
operational bind and immediate explicit refusal, with30s attempts and50s start
intervals. Serial tokens and source clear prevent stale/late success restoring a
client. TunnelBurst/Punch model four send opportunities,3s per send,10s initiator
wait, indirect-port fallback and actual inbound same-IP port priority. Outbound
ACK stops retries without confirming an inbound tunnel or ending the wait.
Delayed ticks do not catch up with a burst; integer/deadline bounds are explicit.
The caller still validates actual source, handler/type/network/lane and epoch.

Seven QTest entries passed in0.36s, including30 commands compared with the exact
bind-result if/else from original runRendezvousSupport. Empty listeners and a
rendezvousFailed notification counter are projection seams; actual owner effects
are excluded. Retry/wait/source-reset/port boundaries use deterministic native
clock tests and inspected original sendPunch/receiveConnect/tunnel source, not
an original timer oracle. Retained75server commands passed in0.39s and100wire
examples in0.34s after extending their shared oracle. [Qualification](qualification.json),
[source scope](source-scope.json).

The next integration dependency is contact address roles. Original UDP contacts
retain external identity and a separate actual transport endpoint, permitting
the same IP with different ports. receiveBind reports the external port;
sendConnect and the relay punch reply use the stored transport port. Rebind
retains the initial contact. The current server memory model stores the external
contact; actual-owner integration must extend it to retain both roles. An earlier
review's 'observed-port reply' wording is corrected to 'reported external port';
the tests already matched the external port, including the distinct transport
port used by the generated Java contact. No code/result regression is implied.

Next: source-bound logical/physical DATA routes and narrow first NAT-handler/
expected-tunnel admission, actual three-party/shared UDP owner, lookup/publication
and room/private integration. A port observation grants no private participant
identity: STS continues to require the selected full signed peer key. No broad
endpoint-check bypass, actual NAT/router/real profile or running-image change is
implemented or claimed. Whole NAT/private conversation chapter remains open.
