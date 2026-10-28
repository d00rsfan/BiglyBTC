# P04-35 — Activity graph rate modes

Status: bounded package qualified and published; STOP at user request.

Final bounded qualification: GCC23 unique affected suites across repaired
attempts; Clang12/leak-enabled sanitizers12; hosted107 at
998db441c4f9d0ed1746ed564eec89f33ad541b9 (run39863152931), SDK39863152936;
private portable13 smoke rows plus2 suites,107 files/19 captures. Prior dist
retained. Product sources are unchanged by the four legacy tab-count repairs.
Full original service/automatic-tag parity is not declared complete. STOP.

Implementation notes and historical qualification progress follow.


Original baseline cb8c6cb9bb97916cbc7e80338a017200ffc3e71f, ActivityView and
GlobalManagerStatsImpl/Average/GeneralUtils. Native source
dbe9230de8b096820a68b2b95e5c51f8e3736e19, parent
6b090767f881eecee8fedc726c94f9a5ed57b358. Source10-path map SHA256
1fdabcc1d959cdbcb91bd32c781eeb9d75a5bf2ef42497804ad21e626e023a94.

Retain optional scalar source rates in each of600 bounded Activity points.
Default graph mode uses original-style10-second sample-delivery windows.
Measured interval and configured combined smoothing remain selectable, with
no fallback or invented smoothed components. Missing values break lines.
The existing six numeric interval facts retain their meanings and IDs.
No sampler, timer, file write or engine/provider start is added.

GCC new rate-mode suite and five affected view/page/actual-worker/startup suites
pass without failures/skips. The two old raw-raster fixtures explicitly select
interval mode; all assertions and deadlines remain. Clang/sanitizers are batched
with Tags once; private portable/hosted107 qualification follows.

This is not full original SpeedGraphic visual averaging, swarm-rate or original
packet-event timing parity. Remaining Statistics services are future work.
