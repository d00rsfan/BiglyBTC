# Focused Host scheduler review and regression sources

Source only. No compilation, Qt/module execution, Java, profile access or native/shared mutations by the reviewer.

Pinned root Host before-image32b6105a700f3bcc4aeffd8d9970e9418a3fd483927599328558124ba9267b90. Frozen scheduler service d0aaeb6537bf048d199c34f9a547e25b10beae8c2835c9c9f6036850a496f496 stays unchanged.

## Concrete fixes

1. A scheduled rate save changes biglybt.config, invalidating the service's cached source CAS. The old Host then called beginAction from that stale plan; simultaneous rate+pause never succeeded. Corrected tick acknowledges only actual first-plan effects, reloads after confirmed config publication, obtains fresh actual limits/rows, and re-evaluates actions against the new revision. Any remaining second-plan rate change is not falsely acknowledged. Successful Options saves similarly refresh the service CAS baseline, preserving same-byte current-session pause ownership.
2. Saved-only rows were assigned high-bit fake admissions and complete=false. Pause could persist pause_data then fail on a nonexistent handle and leave an indeterminate journal. Only actual retained, checked, nonfailed rows are offered now; saved stopped/unadmitted/unchecked completion is never inferred.
3. A malformed later tag could leave earlier cached tag rows marked available. Cache hits now honor the global catalog validity; malformed attributes dictionaries likewise cannot imply known Uncategorized.
4. The scheduler save reply is constructed after tick/reload, so its fresh revision reflects the actual completed save and subsequent internal config publication.

Merge host-fixes.patch or the four exact function fragments. The full source includes the narrow saveOptions reload addition; root has concurrent unrelated Options edits, so do not replace those accidentally. All other Host definitions remain the pinned before-images. Original manual ownership revocation already runs before saved/engine commands; failures are surfaced while explicit commands proceed. Legacy hashes never become service authority. Two-file partial saves remain explicit and retain the actual committed XML snapshot.

## New test wiring (root-owned CMake)

- New shared fixture header tests/scheduler_host_fixture.hpp.
- `scheduler-host`: AUTOMOC QtTest source scheduler_host_tests.cpp, same links as existing options-host (plus scheduler service). Runs default/unbound too. Tests real authenticated module load, actual offline worker saves and stale/CAS partial publication, and actual Tools dialog→worker save.
- `scheduler-host-engine`: AUTOMOC QtTest source scheduler_host_engine_tests.cpp, same links as magnet-bridge / approved source-bound owner tests plus scheduler service. Register ONLY when `NOT BIGLYBTC_PERSISTENT_RUNTIME_MANIFEST STREQUAL ""`. It fails rather than skips if runtime is unqualified. Single/multi actual owner rows check7/9→17/19 limits from nativeSessionReady, genuine scheduler pause, fresh edit after rate CAS update, manual Stop precedence, disabled defaults restore, joined close and untouched payload inode/bytes. A second fixture verifies inherited saved pause/legacy bytes never authorize resume.
- Both targets require `QT_QPA_PLATFORM=offscreen` and `BIGLYBTC_SCHEDULER_TEST_MODULE=$<TARGET_FILE:BiglyBTC-plugin-SpeedScheduler>` in CTest environment, dependency on that module, existing strict C++23 flags, unit label. Suggested deadlines120s offline and180s bound. The module is copied/hash-authenticated inside owned fixtures. No arbitrary profile module or fake successful owner packet is used.

Normal Host fixture routes remain explicit numeric loopback/outgoing TCP only; tracker/DHT/PEX/LSD/UPnP/NAT-PMP/uTP are disabled and no peer hints are supplied. This tests genuine local engine state/rate/control integration, not transfer discovery. Existing generated torrent fixtures create their own isolated seed, but the normal owner is not connected to it. No public networking or original client/profile occurs.
