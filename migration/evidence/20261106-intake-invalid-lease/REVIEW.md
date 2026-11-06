# Invalid-lease intake regression

The full hosted desktop unit run passed241/242 tests; ordinary torrent-intake exposed a moved-from profile lease returning Io instead of the existing UnsafePath contract. The new subscription-intent guard performed a filesystem probe before the old lease validation. Current GCC reproduced the failure. One guard line now validates the lease first; held-lease and pending-intent behavior is unchanged.

Implementation `c34d691a7adc0386e4a653a3eb0a01e4a47e09ea`, native subtree `617b39df4457e57e662a6c1f8788f3e51fc8c37e`; Original `cb8c6cb9bb97916cbc7e80338a017200ffc3e71f`. GCC and ASan/UBSan passed the full14-family/632-assertion intake suite and eight pending guards. Each lane built one executable against four archives and ran three commands, using the existing caches (43.341s/77.962s). No UI/network green suites or real profiles were replayed.

This is a one-line error-contract correction, not broader component completion. The earlier CI failure remains recorded; remote verification of this commit follows publication. The separate development launcher still binds its previously qualified unchanged application image; this targeted run rebuilt the intake test only.
