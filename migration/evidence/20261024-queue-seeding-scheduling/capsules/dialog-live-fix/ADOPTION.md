# Scheduler deleted-control correction

Copy only the two `staging/preview/native` paths after checking the matching
`before` hashes. No CMake or public API change is required. Root owns all builds
and corrected-test execution.

Submission now retains each installed row control with QPointer and refuses a
missing/replaced control before casting Qt's raw cell-widget lookup. The registry
tracks row add/remove/render alongside existing source indices. The actual direct
delete regression remains; it now also proves its QPointer became null. All
existing save-refusal, dirty, pending and unavailable-message assertions remain.

The existing positive test optionally saves `speed-scheduler-preview.png` when
BIGLYBTC_SCHEDULER_CAPTURE_DIRECTORY names an existing absolute, nonsymlink,
owner-writable directory. Empty environment keeps capture IO off. No profile is
opened or directory created. Root may run its existing target with the directory
environment and each chosen QT_SCALE_FACTOR. Options-page captures remain root's
existing hook; this narrow capsule touches no Options source.

One parent-authorized diagnostic reran only the old GCC failing method with the
existing backtrace preload. It reproduced SIGSEGV but emitted no backtrace, so no
exact crashing instruction is claimed. No compilation, corrected run or broader
suite was performed by this author.
