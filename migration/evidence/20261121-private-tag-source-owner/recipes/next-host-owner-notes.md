# Next Host integration facts

PersistentSession currently owns unique ProfileTagRates,lazily opens it in
prepareTagRateSource/startManaged,and resets it on destructor/poison/close paths.
Host initializePersistentOwner skips privateOnly entirely,so a private-only
profile has no normal engine to lend a controller. Normal Host closes private
and mixed owners before the normal engine during profile close.

Use a Host-profile lifetime for the common controller,including private-only
startup. Public PersistentSession must use the same actual controller rather
than create an independent quota scope. Preserve the old default path unless
the exact all-role SDK runtime is enabled. Validate actual profile/startup mode
on controller injection and keep destruction/control on the Host I/O worker.

D9 owner preparation takes the controller only synchronously and retains no raw
pointer. Service start/add can expose explicit synchronous equivalents;Host
passes its own live controller for each preparation. SourcePermit socket turns
must not call the source-reading controller. Service startPending/metadata
promotion also need explicit treatment before declaring the feature complete.

Live assignments must refresh the private passive tag stamps and sealed source
witness after actual common SDK effects. A future refreshed witness for an
already-bound handle cannot depend on the stale pre-add epoch token;use the
actual binding census,current source and all ready SDK groups. Validate/rebase
under trusted source gates after SDK waits,and preserve generic store CAS.

Actual tagged private I2P transport/queue/retirement and cross-owner budgets
need real isolated-router acceptance. Native tests/i2p_multi_host_transfer_probe.cpp
and build/i2p-multi-20261030/acceptance/run.py retain the prior owned /work
router/Host harness;do not use real profiles or the user's live router. D9
source/owner tests provide storage/checking credit only. Global/LAN still open.
